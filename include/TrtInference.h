#ifndef TRTINFERENCE_H
#define TRTINFERENCE_H

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <map> // [新增] 引入 map

class TrtLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
};

class TrtInference {
public:
    TrtInference();
    ~TrtInference();

    bool init(const std::string& enginePath);

    // [保留] 旧的单输入接口
    bool doInference(const float* inputHost, float* outputHost);
    // [保留] 旧的向量多输入接口
    bool doInference(const std::vector<const float*>& inputHosts, float* outputHost);

    // [新增] ★ 按名称传递多输入的接口 ★
    bool doInference(const std::map<std::string, const float*>& namedInputs, float* outputHost);

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

    // [新增] 记录 Tensor 名称到 m_gpuBuffers 索引的映射
    std::map<std::string, int> m_tensorNameToIndex;
};

#endif // TRTINFERENCE_H