/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/kernels/data/experimental/data_service_ops.h"

#include "tensorflow/core/data/service/data_service.h"
#include "tensorflow/core/data/service/grpc_util.h"
#include "tensorflow/core/framework/dataset.h"
#include "tensorflow/core/kernels/data/dataset_utils.h"
#include "tensorflow/core/platform/errors.h"

namespace tensorflow {
namespace data {

namespace {
const int64 kRetryTimeoutMicros = 1000LL * 1000 * 60 * 60;  // 60 minutes.
}

RegisterDatasetOp::RegisterDatasetOp(OpKernelConstruction* ctx)
    : OpKernel(ctx) {
  int64 external_state_policy_int;
  OP_REQUIRES_OK(
      ctx, ctx->GetAttr(kExternalStatePolicy, &external_state_policy_int));
  external_state_policy_ =
      SerializationContext::ExternalStatePolicy(external_state_policy_int);
}

void RegisterDatasetOp::Compute(OpKernelContext* ctx) {
  DatasetBase* dataset;
  OP_REQUIRES_OK(ctx, GetDatasetFromVariantTensor(ctx->input(0), &dataset));

  tstring address;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kAddress, &address));
  OP_REQUIRES(ctx, !address.empty(),
              errors::InvalidArgument(kAddress, " must be non-empty."));

  tstring protocol;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kProtocol, &protocol));
  OP_REQUIRES(ctx, !protocol.empty(),
              errors::InvalidArgument(kProtocol, " must be non-empty."));

  SerializationContext::Params params;
  params.external_state_policy = external_state_policy_;
  SerializationContext serialization_ctx(params);
  GraphDef graph_def;
  OP_REQUIRES_OK(
      ctx, AsGraphDef(ctx, dataset, std::move(serialization_ctx), &graph_def));
  StripDevicePlacement(graph_def.mutable_library());

  DataServiceDispatcherClient client(address, protocol);
  int64 dataset_id;
  int64 deadline_micros = EnvTime::NowMicros() + kRetryTimeoutMicros;
  OP_REQUIRES_OK(
      ctx, grpc_util::Retry(
               [&]() { return client.RegisterDataset(graph_def, dataset_id); },
               /*description=*/"register dataset", deadline_micros));

  Tensor* output;
  OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &output));
  auto output_dataset_id = output->tensor<int64, 0>();
  output_dataset_id() = dataset_id;
}

REGISTER_KERNEL_BUILDER(Name("RegisterDataset").Device(DEVICE_CPU),
                        RegisterDatasetOp);

}  // namespace data
}  // namespace tensorflow
