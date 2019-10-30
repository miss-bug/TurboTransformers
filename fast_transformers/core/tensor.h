#pragma once
#include "fast_transformers/core/blas.h"
#include "fast_transformers/core/enforce.h"
#include "fast_transformers/core/memory.h"
#include <dlpack/dlpack.h>
#include <iostream>
#include <memory>
#include <numeric>

namespace fast_transformers {

namespace core {
namespace details {

struct DLPackManagedTensorDeleter {
  void operator()(DLManagedTensor *tensor) const {
    if (tensor == nullptr) {
      return;
    }
    tensor->deleter(tensor);
  }
};

template <typename T> struct DataTypeTrait;

template <> struct DataTypeTrait<float> {
  enum { DLPackTypeCode = kDLFloat };
};

template <> struct DataTypeTrait<int> {
  enum { DLPackTypeCode = kDLInt };
};

template <> struct DataTypeTrait<int64_t> {
  enum { DLPackTypeCode = kDLInt };
};

template <typename T> static inline bool IsDataType(DLDataType dt) {
  return DataTypeTrait<T>::DLPackTypeCode == dt.code &&
         (dt.bits == 0 || dt.bits == sizeof(T) * 8);
}

extern void DLManagedTensorDeletor(DLManagedTensor *tensor);

} // namespace details

template <typename T>
inline DLManagedTensor *
CreateDLPackTensor(std::initializer_list<int64_t> shape_list) {
  FT_ENFORCE_NE(shape_list.size(), 0, "Shape list should not be empty");
  DLManagedTensor *newTensor = new DLManagedTensor;

  newTensor->dl_tensor.shape = new int64_t[shape_list.size()];
  std::copy(shape_list.begin(), shape_list.end(), newTensor->dl_tensor.shape);

  newTensor->dl_tensor.ctx = {kDLCPU, 0}; // device_type, device_id
  newTensor->dl_tensor.ndim = shape_list.size();

  newTensor->dl_tensor.dtype = {details::DataTypeTrait<T>::DLPackTypeCode,
                                sizeof(T) * 8, 1}; // code, bits, lanes

  newTensor->dl_tensor.strides = nullptr; // TODO
  newTensor->dl_tensor.byte_offset = 0;

  size_t numel = std::accumulate(shape_list.begin(), shape_list.end(), 1,
                                 std::multiplies<int64_t>());
  newTensor->dl_tensor.data = align_alloc_t<T>(numel);

  newTensor->deleter = details::DLManagedTensorDeletor;
  return newTensor;
}

class Tensor {
public:
  explicit Tensor(DLManagedTensor *tensor) : tensor_(tensor) {}

  DLManagedTensor *ToDLPack() {
    FT_ENFORCE_NE(tensor_, nullptr, "The Tensor must contain data");
    return tensor_.release();
  }

  size_t n_dim() const { return tensor_->dl_tensor.ndim; }

  const int64_t &shape(size_t pos) const {
    return tensor_->dl_tensor.shape[pos];
  }

  size_t numel() const {
    if (n_dim() == 0)
      return 0;
    size_t numel_ = 1;
    for (int i = 0; i < n_dim(); ++i)
      numel_ *= shape(i);
    return numel_;
  }

  template <typename T> const T *data() const {
    EnforceDataType<T>(tensor_->dl_tensor);
    return reinterpret_cast<T *>(tensor_->dl_tensor.data);
  }

  template <typename T> T *mutableData() {
    EnforceDataType<T>(tensor_->dl_tensor);
    return reinterpret_cast<T *>(tensor_->dl_tensor.data);
  }

  DLDeviceType device_type() const {
    return tensor_->dl_tensor.ctx.device_type;
  }

  template <typename T> void Print(std::ostream &os) const {
    switch (tensor_->dl_tensor.dtype.code) {
    case kDLInt:
      os << "type: int" << std::endl;
      break;
    case kDLUInt:
      os << "type: unsigned" << std::endl;
      break;
    case kDLFloat:
      os << "type float" << std::endl;
      break;
    default:
      os << "unrecoginized type" << std::endl;
    }

    os << "numel: " << numel() << std::endl;
    os << "n_dim: " << n_dim() << std::endl;
    os << "stride: ";
    if (tensor_->dl_tensor.strides != nullptr) {
      PrintArray(os, tensor_->dl_tensor.strides, tensor_->dl_tensor.ndim);
    } else {
      os << "null";
    }
    os << "\n";
    os << "shape: ";
    PrintArray(os, tensor_->dl_tensor.shape, tensor_->dl_tensor.ndim);
    os << "\n";
    os << "first 10 elems: (";
    int cnt = 10;
    double sum = 0.;
    for (int i = 0; i < numel(); ++i) {
      sum += data<T>()[i];
      if (cnt-- >= 0)
        os << data<T>()[i] << ", ";
    }
    os << ")\n";
    os << "sum is " << sum << std::endl;
  }

private:
  template <typename T>
  static void PrintArray(std::ostream &os, const T *data, size_t n) {
    os << "(";
    for (size_t i = 0; i < n; ++i) {
      if (i != 0) {
        os << ", ";
      }
      os << data[i];
    }
    os << ")";
  }

  template <typename T> static void EnforceDataType(DLTensor t) {
    FT_ENFORCE_EQ(t.byte_offset, 0, "byte_offset must be zero");

    FT_ENFORCE(details::IsDataType<T>(t.dtype),
               "data type mismatch, request %s, actual (%d,%d)",
               typeid(T).name(), t.dtype.code, t.dtype.bits);
  }

private:
  std::unique_ptr<DLManagedTensor, details::DLPackManagedTensorDeleter> tensor_;
};

} // namespace core

} // namespace fast_transformers
