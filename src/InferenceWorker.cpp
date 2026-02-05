#include "../include/InferenceWorker.h"

#include <QDebug>

#include <algorithm>
#include <map>
#include <string>

InferenceWorker::InferenceWorker(std::shared_ptr<TrtInference> trt,
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> queue,
    QObject* parent)
    : QObject(parent)
    , m_trt(std::move(trt))
    , m_queue(std::move(queue)) {
}

void InferenceWorker::process() {
    if (!m_trt || !m_queue) {
        emit inferenceError("Inference worker not initialized.");
        return;
    }

    while (!m_stop.load()) {
        PreprocessPacket packet;
        if (!m_queue->waitPop(packet)) {
            break;
        }

        if (packet.isEndOfStream) {
            emit streamFinished(packet.expectedFrameCount);
            continue;
        }

        if (packet.rgbTensor.empty() || packet.voxelTensor.empty()) {
            continue;
        }

        std::vector<float> output(m_trt->getOutputElementCount(), 0.0f);

        std::map<std::string, const float*> inputs;
        inputs["input_image"] = packet.rgbTensor.data();
        inputs["input_voxel"] = packet.voxelTensor.data();

        if (!m_trt->doInference(inputs, output.data())) {
            emit inferenceError("TensorRT inference failed.");
            continue;
        }

        QImage image = convertOutputToImage(
            output,
            packet.outputWidth,
            packet.outputHeight,
            packet.outputChannels);
        if (!image.isNull()) {
            emit resultReady(packet.frameIndex, image);
        }
    }
}

void InferenceWorker::stop() {
    m_stop.store(true);
    if (m_queue) {
        m_queue->stop();
    }
}

QImage InferenceWorker::convertOutputToImage(
    const std::vector<float>& output,
    int width,
    int height,
    int channels) const {
    if (width <= 0 || height <= 0 || channels <= 0) {
        return QImage();
    }

    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return image;
    }

    const int planeSize = width * height;
    for (int y = 0; y < height; ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;
            auto clamp = [](float value) {
                const float scaled = std::min(1.0f, std::max(0.0f, value)) * 255.0f;
                return static_cast<uchar>(scaled);
            };

            const float r = output[idx];
            const float g = channels > 1 ? output[planeSize + idx] : r;
            const float b = channels > 2 ? output[2 * planeSize + idx] : r;

            row[x * 3 + 0] = clamp(r);
            row[x * 3 + 1] = clamp(g);
            row[x * 3 + 2] = clamp(b);
        }
    }
    return image;
}
