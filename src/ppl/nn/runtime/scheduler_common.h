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

#ifndef _ST_HPC_PPL_NN_RUNTIME_SCHEDULER_COMMON_H_
#define _ST_HPC_PPL_NN_RUNTIME_SCHEDULER_COMMON_H_

#include "ppl/nn/runtime/edge_object.h"
#include "ppl/nn/runtime/profiler.h"
#include <functional>

namespace ppl { namespace nn { namespace utils {

ppl::common::RetCode ExecuteKernel(KernelImpl*, KernelExecContext*,
                                   const std::function<ppl::common::RetCode(EdgeObject*, nodeid_t)>& release_func,
                                   Profiler*);

}}} // namespace ppl::nn::utils

#endif
