#include <ATen/BatchedTensorImpl.h>

#include <ATen/WrapDimUtils.h>
#include <c10/util/Exception.h>

namespace at {

BatchedTensorImpl::BatchedTensorImpl(Tensor value, BatchDims bdims)
  : TensorImpl(
      c10::DispatchKeySet(DispatchKey::Vmap),
      value.dtype(),
      value.device()
    )
  , value_(std::move(value))
  , bdims_(std::move(bdims))
{
  TORCH_INTERNAL_ASSERT(value_.defined());

  const auto public_dims = value_.dim() - bdims_.size();
  const auto value_sizes = value_.sizes();
  sizes_.clear();
  sizes_.reserve(public_dims);
  for (int64_t dim = 0; dim < public_dims; dim++) {
    auto actual_dim = actualDim(dim, /*wrap_dim=*/false);
    sizes_.push_back(value_sizes.at(actual_dim));
  }
  refresh_numel();
}

int64_t BatchedTensorImpl::actualDim(int64_t dim, bool wrap_dim) const {
  if (wrap_dim) {
    const auto ndim = sizes_.size();
    dim = maybe_wrap_dim(dim, ndim);
  }
  auto is_bdim = createBatchDimBitset(bdims_);

  // Example: assume dim = 3, and is_bdim = 10010011000...
  // The 1's are batch dims and 0's are normal dims of the underlying value_ Tensor.
  // actualDim gives us the index of `dim` in the `value_` Tensor, which is equivalent
  // to asking "where does the 3rd (0-indexed) zero occur in the bitset?".
  // The answer to that is index 5.
  int64_t non_bdim_count = 0;
  for (int64_t actual_dim = 0; actual_dim < kVmapMaxTensorDims; actual_dim++) {
    if (is_bdim[actual_dim]) {
      continue;
    }
    if (non_bdim_count == dim) {
      return actual_dim;
    }
    non_bdim_count++;
  }
  // If we hit this assert, then that means
  // `non_bdim_count` + #num_bdims > kVmapMaxTensorDims. We restrict the number
  // of dims a BatchedTensorImpl can have to kVmapMaxTensorDims so this should
  // never be hit.
  TORCH_INTERNAL_ASSERT(false);
}

// The following are publically exposed as methods of Tensor
IntArrayRef BatchedTensorImpl::strides() const {
  TORCH_CHECK(false, "NYI: Getting tensor strides inside of vmap");
}
int64_t BatchedTensorImpl::stride(int64_t d) const {
  TORCH_CHECK(false, "NYI: Getting tensor strides inside of vmap");
}
bool BatchedTensorImpl::is_contiguous(at::MemoryFormat memory_format) const {
  TORCH_CHECK(false, "NYI: querying is_contiguous inside of vmap");
}
const Storage& BatchedTensorImpl::storage() const {
  TORCH_CHECK(false, "Due to limitations, we cannot access the storage() of a tensor from inside of vmap.");
}
int64_t BatchedTensorImpl::storage_offset() const {
  TORCH_CHECK(false, "Due to limitations, we cannot access the storage_offset() of a tensor from inside of vmap.");
}

// The following are some internal inherited methods that we do not support.
// They should never get called.
void BatchedTensorImpl::set_size(int64_t dim, int64_t new_size) {
  TORCH_INTERNAL_ASSERT(false, "Can't set_size for BatchedTensorImpl");
}
void BatchedTensorImpl::set_stride(int64_t dim, int64_t new_stride) {
  TORCH_INTERNAL_ASSERT(false, "Can't set_stride for BatchedTensorImpl");
}
void BatchedTensorImpl::set_storage_offset(int64_t storage_offset) {
  TORCH_INTERNAL_ASSERT(false, "Can't set_storage_offset for BatchedTensorImpl");
}
bool BatchedTensorImpl::has_storage() const {
  TORCH_INTERNAL_ASSERT(false, "Can't query has_storage for BatchedTensorImpl");
}

Tensor addBatchDim(const Tensor& tensor, int64_t level, int64_t dim) {
  if (!isBatched(tensor)) {
    BatchDims bdims;
    bdims.push_back({level, dim});
    return at::detail::make_tensor<BatchedTensorImpl>(tensor, std::move(bdims));
  }
  const auto* batched = getBatched(tensor);
  BatchDims new_bdims = { batched->bdims().begin(), batched->bdims().end() };
  auto actual_bdim = batched->actualDim(dim, /*wrap_dim=*/true);
  new_bdims.push_back({level, actual_bdim});
  return makeBatched(batched->value(), std::move(new_bdims));
}

} // namespace at
