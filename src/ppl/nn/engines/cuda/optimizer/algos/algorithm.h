// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef _ST_HPC_PPL_NN_ENGINES_CUDA_OPTIMIZER_ALGOS_ALGORITHM_H_
#define _ST_HPC_PPL_NN_ENGINES_CUDA_OPTIMIZER_ALGOS_ALGORITHM_H_

#include <set>
#include <map>
#include <vector>
#include <string>

#include "ppl/common/types.h"
#include "ppl/nn/ir/graph.h"
#include "ppl/nn/engines/cuda/optimizer/opt_kernel.h"
#include "ppl/common/destructor.h"

#define ALGO_MAX_TIME (3.0e+10)

#define ALLOC_BUFFERF_FOR_ALGO_SELECT(___buffer_name___, ___size___, ___ret___)                        \
    BufferDesc ___buffer_name___;                                                                      \
    status = options.opt_stage_device->ReallocWithRandomValue(___size___, &___buffer_name___);         \
    if (status != RC_SUCCESS) {                                                                        \
        LOG(DEBUG) << "alloc " #___buffer_name___ " tensor failed";                                    \
        return ___ret___;                                                                              \
    }                                                                                                  \
    ppl::common::Destructor __##___buffer_name___##_guard__([&options, &___buffer_name___]() -> void { \
        options.opt_stage_device->Free(&___buffer_name___);                                            \
    });

namespace ppl { namespace nn { namespace cuda {

class Algorithm {
public:
    virtual ~Algorithm() {}
    virtual bool IsRepeatable(const OptKernelOptions& options) const {
        return true;
    }
    virtual bool IsSupported(const ir::Node* node, const OptKernelOptions& options,
                             ppl::common::dataformat_t input_format) const {
        return true;
    }
    virtual bool CanSupportDynamic(const OptKernelOptions& options) const {
        return true;
    }

    virtual const std::map<ppl::common::dataformat_t, std::set<ppl::common::dataformat_t>> Getformats(
        const std::string& type_name) const = 0;
    virtual void GetAttrParam(void*& param) const = 0;
    virtual void DeleteAttrParam(void*& param) = 0;

    virtual double ExcuteTimer(const ir::Node* node, OptKernelOptions& options) = 0;
    virtual ppl::common::RetCode ModifyParam(ir::Node* node, OptKernelOptions& options) = 0;
    virtual void ReshapeOnEdges(const ir::Node* node, std::map<edgeid_t, std::unique_ptr<TensorImpl>>* tensors,
                                ppl::common::dataformat_t input_format, ppl::common::dataformat_t output_format) = 0;
};

}}} // namespace ppl::nn::cuda

#endif
