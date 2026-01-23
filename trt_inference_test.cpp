#include <QCoreApplication>
#include <QDebug>
#include <opencv2/opencv.hpp>

#include "include/TrtInference.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        qInfo() << "Usage: trt_inference_test <engine_path> <image_path> [output_path]";
        return 1;
    }

    std::string enginePath = argv[1];
    std::string imagePath = argv[2];
    std::string outputPath = argc > 3 ? argv[3] : "trt_output.png";

    TrtInference trt;
    if (!trt.init(enginePath)) {
        qCritical() << "Failed to initialize TensorRT engine.";
        return 1;
    }

    cv::Mat inputBgr = cv::imread(imagePath);
    if (inputBgr.empty()) {
        qCritical() << "Failed to load image:" << QString::fromStdString(imagePath);
        return 1;
    }

    if (trt.getInputCount() < 1) {
        qCritical() << "Engine has no input bindings.";
        return 1;
    }

    nvinfer1::Dims inputDims = trt.getInputDims(0);
    int inputH = inputDims.d[inputDims.nbDims - 2];
    int inputW = inputDims.d[inputDims.nbDims - 1];

    cv::Mat resized;
    cv::resize(inputBgr, resized, cv::Size(inputW, inputH));
    cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);

    std::vector<float> inputTensor((size_t)3 * inputH * inputW, 0.0f);
    for (int y = 0; y < inputH; ++y) {
        const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
        for (int x = 0; x < inputW; ++x) {
            size_t idx = (size_t)y * inputW + x;
            inputTensor[idx] = row[x][0] / 255.0f;
            inputTensor[(size_t)inputH * inputW + idx] = row[x][1] / 255.0f;
            inputTensor[(size_t)2 * inputH * inputW + idx] = row[x][2] / 255.0f;
        }
    }

    std::vector<const float*> inputs;
    inputs.push_back(inputTensor.data());
    std::vector<std::vector<float>> extraInputs;

    for (int i = 1; i < trt.getInputCount(); ++i) {
        nvinfer1::Dims dims = trt.getInputDims(i);
        size_t elements = 1;
        for (int j = 0; j < dims.nbDims; ++j) {
            elements *= static_cast<size_t>(dims.d[j]);
        }
        extraInputs.emplace_back(elements, 0.0f);
        inputs.push_back(extraInputs.back().data());
    }

    std::vector<float> output(trt.getOutputElementCount(), 0.0f);
    if (!trt.doInference(inputs, output.data())) {
        qCritical() << "Inference failed.";
        return 1;
    }

    nvinfer1::Dims outDims = trt.getOutputDims();
    int outH = outDims.d[outDims.nbDims - 2];
    int outW = outDims.d[outDims.nbDims - 1];
    int outC = outDims.d[outDims.nbDims - 3];
    cv::Mat outputMat(outH, outW, CV_8UC3);

    int planeSize = outH * outW;
    for (int y = 0; y < outH; ++y) {
        cv::Vec3b* row = outputMat.ptr<cv::Vec3b>(y);
        for (int x = 0; x < outW; ++x) {
            int idx = y * outW + x;
            float r = output[idx];
            float g = outC > 1 ? output[planeSize + idx] : r;
            float b = outC > 2 ? output[2 * planeSize + idx] : r;
            row[x] = cv::Vec3b(
                static_cast<unsigned char>(std::min(1.0f, std::max(0.0f, b)) * 255.0f),
                static_cast<unsigned char>(std::min(1.0f, std::max(0.0f, g)) * 255.0f),
                static_cast<unsigned char>(std::min(1.0f, std::max(0.0f, r)) * 255.0f)
            );
        }
    }

    if (!cv::imwrite(outputPath, outputMat)) {
        qCritical() << "Failed to save output image to" << QString::fromStdString(outputPath);
        return 1;
    }

    qInfo() << "Inference complete. Output saved to" << QString::fromStdString(outputPath);
    return 0;
}
