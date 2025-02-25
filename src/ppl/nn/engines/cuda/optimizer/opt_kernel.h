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

#ifndef _ST_HPC_PPL_NN_ENGINES_CUDA_OPTIMIZER_OPT_KERNEL_H_
#define _ST_HPC_PPL_NN_ENGINES_CUDA_OPTIMIZER_OPT_KERNEL_H_

#include "ppl/nn/runtime/opt_kernel.h"

#include <set>
#include <map>
#include <string>

#include "ppl/common/retcode.h"
#include "ppl/common/types.h"
#include "ppl/nn/common/logger.h"
#include "ppl/nn/ir/graph.h"
#include "ppl/nn/engines/cuda/engine.h"
#include "ppl/nn/engines/cuda/cuda_device.h"
#include "ppl/nn/engines/cuda/cuda_common_param.h"
#include "ppl/nn/runtime/tensor_impl.h"
#include "ppl/nn/runtime/runtime_graph_info.h"
#include "ppl/nn/runtime/runtime_partition_info.h"

namespace ppl { namespace nn { namespace utils {
struct SharedResource;
}}} // namespace ppl::nn::utils

namespace ppl { namespace nn { namespace cuda {

struct OptKernelOptions final {
    OptKernelOptions(ir::Graph* graph, RuntimePartitionInfo* info, const utils::SharedResource* r, CudaArgs* args,
                     CompileInfo* compile_set, CudaDevice* opt_stage_dev, CudaDevice* reserved_data_dev,
                     std::map<edgeid_t, std::unique_ptr<TensorImpl>>* tensors, std::vector<CudaTensorQuant>* quants,
                     std::map<std::string, CudaArgs::AlgoSelects>* algos)
        : graph(graph)
        , info(info)
        , resource(r)
        , args(args)
        , compile_set(compile_set)
        , opt_stage_device(opt_stage_dev)
        , reserved_data_device(reserved_data_dev)
        , tensors(tensors)
        , quants(quants)
        , algos(algos) {}

    OptKernelOptions(ir::Graph* graph, RuntimePartitionInfo* info, const utils::SharedResource* r,
                     std::map<edgeid_t, std::unique_ptr<TensorImpl>>* tensors)
        : graph(graph), info(info), resource(r), tensors(tensors) {}
    OptKernelOptions(ir::Graph* graph, RuntimePartitionInfo* info, const utils::SharedResource* r,
                     CudaDevice* opt_stage_dev, CudaDevice* reserved_data_dev, CUDAModuleManager* manager)
        : graph(graph)
        , info(info)
        , resource(r)
        , opt_stage_device(opt_stage_dev)
        , reserved_data_device(reserved_data_dev)
        , cuda_module_manager(manager) {}
    OptKernelOptions(ir::Graph* graph, const utils::SharedResource* r) : graph(graph), resource(r) {}

    ir::Graph* graph;
    RuntimePartitionInfo* info;
    const utils::SharedResource* resource;
    CudaArgs* args;
    CompileInfo* compile_set;
    CudaDevice* opt_stage_device; // device for optimization stage. will be destroyed after Engine::ProcessGraph()
    CudaDevice* reserved_data_device; // used to store data that are used in runtime stage
    std::map<edgeid_t, std::unique_ptr<TensorImpl>>* tensors;
    std::vector<CudaTensorQuant>* quants;
    std::map<std::string, CudaArgs::AlgoSelects>* algos;
    void* param;
    CUDAModuleManager* cuda_module_manager;
};

class CudaOptKernel : public OptKernel {
public:
    CudaOptKernel(const ir::Node* node) : OptKernel(node) {}
    virtual ~CudaOptKernel() {}

    virtual ppl::common::RetCode Init(const OptKernelOptions& options) = 0;
    virtual ppl::common::RetCode Finalize(const OptKernelOptions& options) = 0;
    virtual void* GetParam() {
        return nullptr;
    }
    virtual void CopyParam(void*& param) {
        param = nullptr;
    }
    virtual bool CompareParam(CudaOptKernel* other) {
        return false;
    }

    CudaCommonParam* GetCommparam() {
        return &common_param_;
    }
    const CudaCommonParam* GetCommparam() const {
        return &common_param_;
    }

    ppl::common::RetCode InferType(InputOutputInfo* info, std::vector<CudaTensorQuant>* quant,
                                   ppl::common::datatype_t type) const {
        return infer_type_func_(info, quant, type);
    }

    ppl::common::RetCode InferDims(InputOutputInfo* info) const {
        return infer_dims_func_(info);
    }

    ppl::common::RetCode InferUnsafeDims(InputOutputInfo* info, std::set<uint32_t>* illegal_inputs) const {
        return infer_unsafe_dims_func_(info, illegal_inputs);
    }

#ifdef PPLNN_ENABLE_PMX_MODEL
    ppl::common::RetCode SerializeData(const pmx::SerializationContext&, utils::DataStream*) const override {
        return ppl::common::RC_UNSUPPORTED;
    }
    ppl::common::RetCode DeserializeData(const pmx::DeserializationContext&, const void*, uint64_t) override {
        return ppl::common::RC_UNSUPPORTED;
    }
#endif

protected:
    ppl::common::RetCode SetCommonParam(const OptKernelOptions&);

    template <typename T>
    ppl::common::RetCode GenericLoadParam(const OptKernelOptions& options, T* param) {
        auto node = GetNode();
        auto graph_data = options.graph->data.get();
        auto param_ref = graph_data->attrs.find(node->GetId());
        if (param_ref == graph_data->attrs.end()) {
            return ppl::common::RC_NOT_FOUND;
        }
        *param = *((const T*)param_ref->second.get());
        return ppl::common::RC_SUCCESS;
    }

    template <typename KernelType, typename ParamType>
    KernelType* CreateKernelImplWithParam(const ParamType* param) const {
        auto kernel = new KernelType(GetNode());
        auto status = kernel->Init();
        if (status != ppl::common::RC_SUCCESS) {
            delete kernel;
            return nullptr;
        }
        kernel->SetParam(param);
        kernel->SetCommonParam(&common_param_);
        kernel->SetReshapeFunc([this](InputOutputInfo* info) -> ppl::common::RetCode {
            auto status = infer_dims_func_(info);
            GenericInferTypeAndFormat(info);
            return status;
        });
        return kernel;
    }

    template <typename KernelType>
    KernelType* CreateKernelImplWithoutParam() const {
        auto kernel = new KernelType(GetNode());
        auto status = kernel->Init();
        if (status != ppl::common::RC_SUCCESS) {
            delete kernel;
            return nullptr;
        }
        kernel->SetCommonParam(&common_param_);
        kernel->SetReshapeFunc([this](InputOutputInfo* info) -> ppl::common::RetCode {
            auto status = infer_dims_func_(info);
            GenericInferTypeAndFormat(info);
            return status;
        });
        return kernel;
    }

    static ppl::common::RetCode GenericInferDims(InputOutputInfo* info) {
        const TensorShape& in_shape0 = *info->GetInput<TensorImpl>(0)->GetShape();
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            info->GetOutput<TensorImpl>(i)->GetShape()->Reshape(in_shape0.GetDims(), in_shape0.GetRealDimCount());
        }
        return ppl::common::RC_SUCCESS;
    }

    static ppl::common::RetCode GenericUnsafeInferDims(InputOutputInfo* info, std::set<uint32_t>* mask) {
        const TensorShape& in_shape0 = *info->GetInput<TensorImpl>(0)->GetShape();
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            info->GetOutput<TensorImpl>(i)->GetShape()->Reshape(in_shape0.GetDims(), in_shape0.GetRealDimCount());
        }
        return ppl::common::RC_SUCCESS;
    }

    static ppl::common::RetCode CopyQuantType(InputOutputInfo* info, std::vector<CudaTensorQuant>* quant) {
        for (uint32_t i = 0; i < info->GetInputCount(); ++i) {
            auto edge_id = info->GetInput<TensorImpl>(i)->GetEdge()->GetId();
            auto& in_quant = quant->at(edge_id);
            if (in_quant.type != ppl::common::DATATYPE_UNKNOWN) {
                TensorShape& shape = *info->GetInput<TensorImpl>(i)->GetShape();
                shape.SetDataType(in_quant.type);
            }
        }
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            auto edge_id = info->GetOutput<TensorImpl>(i)->GetEdge()->GetId();
            auto& out_quant = quant->at(edge_id);
            if (out_quant.type != ppl::common::DATATYPE_UNKNOWN) {
                TensorShape& shape = *info->GetOutput<TensorImpl>(i)->GetShape();
                shape.SetDataType(out_quant.type);
            }
        }
        return ppl::common::RC_SUCCESS;
    }

    static ppl::common::RetCode UnifyToOutputQuant(InputOutputInfo* info, std::vector<CudaTensorQuant>* quant) {
        auto temp_edge_id = info->GetOutput<TensorImpl>(0)->GetEdge()->GetId();
        auto& temp_quant = quant->at(temp_edge_id);
        if (temp_quant.type == ppl::common::DATATYPE_UNKNOWN) {
            return ppl::common::RC_INVALID_VALUE;
        }
        for (uint32_t i = 0; i < info->GetInputCount(); ++i) {
            auto in_edge_id = info->GetInput<TensorImpl>(i)->GetEdge()->GetId();
            auto& in_quant = quant->at(in_edge_id);
            in_quant = temp_quant;
            TensorShape& in_shape = *info->GetInput<TensorImpl>(i)->GetShape();
            in_shape.SetDataType(in_quant.type);
        }
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            auto out_edge_id = info->GetOutput<TensorImpl>(i)->GetEdge()->GetId();
            auto& out_quant = quant->at(out_edge_id);
            out_quant = temp_quant;
            TensorShape& out_shape = *info->GetOutput<TensorImpl>(i)->GetShape();
            out_shape.SetDataType(out_quant.type);
        }
        return ppl::common::RC_SUCCESS;
    }

    static ppl::common::RetCode InferDefaultType(InputOutputInfo* info, ppl::common::datatype_t type) {
        for (uint32_t i = 0; i < info->GetInputCount(); ++i) {
            auto impl = info->GetInput<TensorImpl>(i);
            if (impl == nullptr)
                continue;
            auto in_shape = impl->GetShape();
            auto date_type = in_shape->GetDataType();
            if (date_type == ppl::common::DATATYPE_UNKNOWN || date_type == ppl::common::DATATYPE_FLOAT32 ||
                date_type == ppl::common::DATATYPE_FLOAT16 || date_type == ppl::common::DATATYPE_INT8) {
                in_shape->SetDataType(type);
            }
        }
        auto input_type = info->GetInput<TensorImpl>(0)->GetShape()->GetDataType();
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            auto impl = info->GetOutput<TensorImpl>(i);
            if (impl == nullptr)
                continue;
            auto out_shape = impl->GetShape();
            out_shape->SetDataType(input_type);
        }
        return ppl::common::RC_SUCCESS;
    }

    static ppl::common::RetCode InferInheritedType(InputOutputInfo* info) {
        TensorShape& in_shape = *info->GetInput<TensorImpl>(0)->GetShape();
        if (in_shape.GetDataType() == ppl::common::DATATYPE_UNKNOWN) {
            LOG(DEBUG) << "Input edge has unknown type.";
        }
        if (in_shape.GetDataType() == ppl::common::DATATYPE_INT8) {
            in_shape.SetDataType(ppl::common::DATATYPE_FLOAT16);
        }
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            TensorShape& out_shape = *info->GetOutput<TensorImpl>(i)->GetShape();
            out_shape.SetDataType(in_shape.GetDataType());
        }
        return ppl::common::RC_SUCCESS;
    }

    static ppl::common::RetCode InferHighestType(InputOutputInfo* info, ppl::common::datatype_t type,
                                                 uint64_t mask = 0) {
        ppl::common::datatype_t highest = type;
        for (uint32_t i = 0; i < info->GetInputCount(); ++i) {
            if (i < 64 && mask & (1 << i)) {
                continue;
            }
            auto in_shape = info->GetInput<TensorImpl>(i)->GetShape();
            if (in_shape->GetDataType() > highest) {
                highest = in_shape->GetDataType();
            }
        }
        for (uint32_t i = 0; i < info->GetInputCount(); ++i) {
            auto in_shape = info->GetInput<TensorImpl>(i)->GetShape();
            in_shape->SetDataType(highest);
        }
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            auto out_shape = info->GetOutput<TensorImpl>(i)->GetShape();
            out_shape->SetDataType(highest);
        }
        if (highest == ppl::common::DATATYPE_UNKNOWN) {
            LOG(DEBUG) << "all inputs is constants or some inputs have unknown input type.";
        }
        return ppl::common::RC_SUCCESS;
    }

private:
    void GenericInferTypeAndFormat(InputOutputInfo* info) const {
        auto pre_shape = info->GetInput<TensorImpl>(0)->GetShape();
        for (uint32_t i = 0; i < info->GetOutputCount(); ++i) {
            TensorShape& tensor_shape = *info->GetOutput<TensorImpl>(i)->GetShape();
            auto edge_id = info->GetOutput<TensorImpl>(i)->GetEdge()->GetId();
            auto data_type = (*common_param_.cuda_tensor_info)[edge_id].type;
            auto data_foramt = (*common_param_.cuda_tensor_info)[edge_id].format;
            tensor_shape.SetDataType(data_type == ppl::common::DATATYPE_UNKNOWN ? pre_shape->GetDataType() : data_type);
            tensor_shape.SetDataFormat(data_foramt == ppl::common::DATAFORMAT_UNKNOWN ? pre_shape->GetDataFormat()
                                                                                      : data_foramt);
        }
    }

protected:
    std::function<ppl::common::RetCode(InputOutputInfo*)> infer_dims_func_;
    std::function<ppl::common::RetCode(InputOutputInfo*, std::set<uint32_t>*)> infer_unsafe_dims_func_ =
        GenericUnsafeInferDims;
    std::function<ppl::common::RetCode(InputOutputInfo*, std::vector<CudaTensorQuant>*, ppl::common::datatype_t)>
        infer_type_func_;

private:
    CudaCommonParam common_param_;
};

}}} // namespace ppl::nn::cuda

#endif
