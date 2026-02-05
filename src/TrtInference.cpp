#include "../include/TrtInference.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <numeric>

void TrtLogger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        std::cout << "[TensorRT] " << msg << std::endl;
    }
}

TrtInference::TrtInference() = default;

TrtInference::~TrtInference() {
    if (m_stream) {
        cudaStreamDestroy(m_stream);
        m_stream = nullptr;
    }
    for (void* buffer : m_gpuBuffers) {
        cudaFree(buffer);
    }
}

bool TrtInference::init(const std::string& enginePath) {
    std::ifstream file(enginePath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open engine file: " << enginePath << std::endl;
        return false;
    }

    file.seekg(0, std::ifstream::end);
    size_t size = file.tellg();
    file.seekg(0, std::ifstream::beg);

    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    m_runtime.reset(nvinfer1::createInferRuntime(m_logger));
    if (!m_runtime) {
        std::cerr << "Failed to create TensorRT runtime." << std::endl;
        return false;
    }

    m_engine.reset(m_runtime->deserializeCudaEngine(engineData.data(), size));
    if (!m_engine) {
        std::cerr << "Failed to deserialize TensorRT engine." << std::endl;
        return false;
    }

    m_context.reset(m_engine->createExecutionContext());
    if (!m_context) {
        std::cerr << "Failed to create TensorRT execution context." << std::endl;
        return false;
    }

    if (cudaStreamCreate(&m_stream) != cudaSuccess) {
        std::cerr << "Failed to create CUDA stream." << std::endl;
        return false;
    }

    return allocateBuffers();
}

bool TrtInference::allocateBuffers() {
    int32_t nbIOTensors = m_engine->getNbIOTensors();

    m_gpuBuffers.assign(nbIOTensors, nullptr);
    m_bindingSizes.assign(nbIOTensors, 0);
    m_inputIndices.clear();

    // [新增] 清空字典映射
    m_tensorNameToIndex.clear();
    m_outputIndex = -1;

    for (int32_t i = 0; i < nbIOTensors; ++i) {
        const char* tensorName = m_engine->getIOTensorName(i);

        // [新增] 记录名称到索引的映射
        m_tensorNameToIndex[std::string(tensorName)] = i;

        nvinfer1::Dims dims = m_engine->getTensorShape(tensorName);

        size_t elements = getSizeByDim(dims);
        size_t bytes = elements * sizeof(float);
        m_bindingSizes[i] = bytes;

        if (m_engine->getTensorIOMode(tensorName) == nvinfer1::TensorIOMode::kINPUT) {
            m_inputIndices.push_back(i);
        }
        else {
            m_outputIndex = i;
        }

        if (cudaMalloc(&m_gpuBuffers[i], bytes) != cudaSuccess) {
            std::cerr << "Failed to allocate CUDA buffer for tensor: " << tensorName << std::endl;
            return false;
        }
    }

    return m_outputIndex >= 0;
}

// 保留旧版单 Tensor 接口
bool TrtInference::doInference(const float* inputHost, float* outputHost) {
    std::vector<const float*> inputs;
    inputs.push_back(inputHost);
    return doInference(inputs, outputHost);
}

// 保留旧版数组接口
bool TrtInference::doInference(const std::vector<const float*>& inputHosts, float* outputHost) {
    if (!m_context) return false;
    if (inputHosts.size() != m_inputIndices.size()) return false;

    for (size_t i = 0; i < inputHosts.size(); ++i) {
        int bindingIndex = m_inputIndices[i];
        cudaMemcpyAsync(m_gpuBuffers[bindingIndex], inputHosts[i], m_bindingSizes[bindingIndex], cudaMemcpyHostToDevice, m_stream);
    }

    int32_t nbIOTensors = m_engine->getNbIOTensors();
    for (int32_t i = 0; i < nbIOTensors; ++i) {
        const char* tensorName = m_engine->getIOTensorName(i);
        m_context->setTensorAddress(tensorName, m_gpuBuffers[i]);
    }

    if (!m_context->enqueueV3(m_stream)) return false;

    cudaMemcpyAsync(outputHost, m_gpuBuffers[m_outputIndex], m_bindingSizes[m_outputIndex], cudaMemcpyDeviceToHost, m_stream);
    cudaStreamSynchronize(m_stream);

    return true;
}

// =========================================================
// [核心新增] 全新的双流/按字典名称传参推理接口
// =========================================================
bool TrtInference::doInference(const std::map<std::string, const float*>& namedInputs, float* outputHost) {
    if (!m_context) {
        return false;
    }

    // 1. Copy Inputs (Host -> Device)
    for (const auto& pair : namedInputs) {
        const std::string& name = pair.first;
        const float* hostPtr = pair.second;

        // 查找该名称对应的索引
        auto it = m_tensorNameToIndex.find(name);
        if (it == m_tensorNameToIndex.end()) {
            std::cerr << "Input tensor name not found in engine: " << name << std::endl;
            return false;
        }

        int index = it->second;
        // 异步拷贝内存到对应的 GPU Buffer
        cudaMemcpyAsync(m_gpuBuffers[index], hostPtr, m_bindingSizes[index], cudaMemcpyHostToDevice, m_stream);
    }

    // 2. Set Tensor Addresses (TensorRT 10.x 要求)
    int32_t nbIOTensors = m_engine->getNbIOTensors();
    for (int32_t i = 0; i < nbIOTensors; ++i) {
        const char* tensorName = m_engine->getIOTensorName(i);
        m_context->setTensorAddress(tensorName, m_gpuBuffers[i]);
    }

    // 3. Run Inference (enqueueV3)
    if (!m_context->enqueueV3(m_stream)) {
        std::cerr << "TensorRT enqueueV3 failed." << std::endl;
        return false;
    }

    // 4. Copy Output (Device -> Host)
    cudaMemcpyAsync(outputHost, m_gpuBuffers[m_outputIndex], m_bindingSizes[m_outputIndex], cudaMemcpyDeviceToHost, m_stream);
    cudaStreamSynchronize(m_stream);

    return true;
}

int TrtInference::getInputCount() const {
    return static_cast<int>(m_inputIndices.size());
}

nvinfer1::Dims TrtInference::getInputDims(int index) const {
    if (index < 0 || index >= static_cast<int>(m_inputIndices.size())) {
        return nvinfer1::Dims{};
    }
    int bindingIndex = m_inputIndices[index];
    const char* name = m_engine->getIOTensorName(bindingIndex);
    return m_engine->getTensorShape(name);
}

nvinfer1::Dims TrtInference::getOutputDims() const {
    if (m_outputIndex < 0) {
        return nvinfer1::Dims{};
    }
    const char* name = m_engine->getIOTensorName(m_outputIndex);
    return m_engine->getTensorShape(name);
}

size_t TrtInference::getOutputElementCount() const {
    if (m_outputIndex < 0) {
        return 0;
    }
    const char* name = m_engine->getIOTensorName(m_outputIndex);
    return getSizeByDim(m_engine->getTensorShape(name));
}

size_t TrtInference::getSizeByDim(const nvinfer1::Dims& dims) const {
    size_t size = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        if (dims.d[i] > 0)
            size *= static_cast<size_t>(dims.d[i]);
    }
    return size;
}