// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#include "tensorflow/core/framework/common_shape_fns.h"
#include "tensorflow/core/framework/numeric_op.h"
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/shape_inference.h"
#include "tensorflow/core/lib/core/status.h"

using namespace tensorflow;  // NOLINT(build/namespaces)

using shape_inference::InferenceContext;
using shape_inference::ShapeHandle;

namespace {

// Copy from https://github.com/tensorflow/tensorflow/blob/r1.13/tensorflow/core/ops/math_ops.cc#L954
Status SparseSegmentReductionGradShapeFn(InferenceContext* c) {
  ShapeHandle data_shape;
  TF_RETURN_IF_ERROR(c->WithRankAtLeast(c->input(0), 1, &data_shape));

  ShapeHandle indices_shape;
  TF_RETURN_IF_ERROR(c->WithRank(c->input(1), 1, &indices_shape));

  // indices and segment_ids should merge cleanly.
  ShapeHandle unused;
  TF_RETURN_IF_ERROR(c->Merge(c->input(2), indices_shape, &unused));

  // output_dim0 should be a scalar
  TF_RETURN_IF_ERROR(c->WithRank(c->input(3), 0, &unused));

  ShapeHandle subshape;
  TF_RETURN_IF_ERROR(c->Subshape(data_shape, 1, &subshape));

  const Tensor* dim0 = c->input_tensor(3);
  ShapeHandle dim0_shape;
  if (dim0 == nullptr) {
    // We don't have the value at inference time, so the output
    // shape is unknown.
    dim0_shape = c->Vector(InferenceContext::kUnknownDim);
  } else {
    auto dim0_value = dim0->scalar<int32>()();
    if (dim0_value < 0) {
      return errors::InvalidArgument(
          "Cannot specify a negative value for output_dim0");
    }
    dim0_shape = c->Vector(dim0_value);
  }

  ShapeHandle out;
  TF_RETURN_IF_ERROR(c->Concatenate(dim0_shape, subshape, &out));
  c->set_output(0, out);
  return Status::OK();
}
}  // namespace

REGISTER_OP("SparseSegmentMeanGradGrad")
    .Input("grad: T")
    .Input("indices: Tidx")
    .Input("segment_ids: int32")
    .Input("output_dim0: int32")
    .Output("output: T")
    .Attr("T: {float, double}")
    .Attr("Tidx: {int32, int64} = DT_INT32")
    .SetShapeFn(SparseSegmentReductionGradShapeFn);

REGISTER_OP("SparseSegmentSqrtNGradGrad")
    .Input("grad: T")
    .Input("indices: Tidx")
    .Input("segment_ids: int32")
    .Input("output_dim0: int32")
    .Output("output: T")
    .Attr("T: {float, double}")
    .Attr("Tidx: {int32, int64} = DT_INT32")
    .SetShapeFn(SparseSegmentReductionGradShapeFn);
