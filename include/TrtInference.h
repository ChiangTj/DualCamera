#ifndef TRTINFERENCE_H
#define TRTINFERENCE_H

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class TrtLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
};

class TrtInference {
public:
    TrtInference();
    ~TrtInference();

    bool init(const std::string& enginePath);
    bool doInference(const float* inputHost, float* outputHost);
    bool doInference(const std::vector<const float*>& inputHosts, float* outputHost);

    int getInputCount() const;
    nvinfer1::Dims getInputDims(int index) const;
    nvinfer1::Dims getOutputDims() const;
    size_t getOutputElementCount() const;

private:
    bool allocateBuffers();
    size_t getSizeByDim(const nvinfer1::Dims& dims) const;

    TrtLogger m_logger;
    std::unique_ptr<nvinfer1::IRuntime> m_runtime;
    std::unique_ptr<nvinfer1::ICudaEngine> m_engine;
    std::unique_ptr<nvinfer1::IExecutionContext> m_context;
    std::vector<void*> m_gpuBuffers;
    std::vector<size_t> m_bindingSizes;
    std::vector<int> m_inputIndices;
    int m_outputIndex = -1;
    cudaStream_t m_stream = nullptr;
};

#endif // TRTINFERENCE_H
