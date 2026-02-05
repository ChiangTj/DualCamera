#include "../include/TrtInference.h"

#include <fstream>
#include <iostream>

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
    int bindings = m_engine->getNbBindings();
    m_gpuBuffers.assign(bindings, nullptr);
    m_bindingSizes.assign(bindings, 0);
    m_inputIndices.clear();
    m_outputIndex = -1;

    for (int i = 0; i < bindings; ++i) {
        nvinfer1::Dims dims = m_engine->getBindingDimensions(i);
        size_t elements = getSizeByDim(dims);
        size_t bytes = elements * sizeof(float);
        m_bindingSizes[i] = bytes;

        if (m_engine->bindingIsInput(i)) {
            m_inputIndices.push_back(i);
        } else {
            m_outputIndex = i;
        }

        if (cudaMalloc(&m_gpuBuffers[i], bytes) != cudaSuccess) {
            std::cerr << "Failed to allocate CUDA buffer for binding " << i << std::endl;
            return false;
        }
    }

    return m_outputIndex >= 0;
}

bool TrtInference::doInference(const float* inputHost, float* outputHost) {
    std::vector<const float*> inputs;
    inputs.push_back(inputHost);
    return doInference(inputs, outputHost);
}

bool TrtInference::doInference(const std::vector<const float*>& inputHosts, float* outputHost) {
    if (!m_context) {
        return false;
    }
    if (inputHosts.size() != m_inputIndices.size()) {
        std::cerr << "Input count mismatch: expected " << m_inputIndices.size()
                  << " got " << inputHosts.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < inputHosts.size(); ++i) {
        int binding = m_inputIndices[i];
        cudaMemcpyAsync(m_gpuBuffers[binding], inputHosts[i], m_bindingSizes[binding], cudaMemcpyHostToDevice, m_stream);
    }

    if (!m_context->enqueueV2(m_gpuBuffers.data(), m_stream, nullptr)) {
        std::cerr << "TensorRT enqueue failed." << std::endl;
        return false;
    }

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
    return m_engine->getBindingDimensions(m_inputIndices[index]);
}

nvinfer1::Dims TrtInference::getOutputDims() const {
    if (m_outputIndex < 0) {
        return nvinfer1::Dims{};
    }
    return m_engine->getBindingDimensions(m_outputIndex);
}

size_t TrtInference::getOutputElementCount() const {
    if (m_outputIndex < 0) {
        return 0;
    }
    return getSizeByDim(m_engine->getBindingDimensions(m_outputIndex));
}

size_t TrtInference::getSizeByDim(const nvinfer1::Dims& dims) const {
    size_t size = 1;
    for (int i = 0; i < dims.nbDims; ++i) {
        size *= static_cast<size_t>(dims.d[i]);
    }
    return size;
}
