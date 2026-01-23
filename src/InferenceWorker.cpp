#include "../include/InferenceWorker.h"

#include <QDebug>
#include <algorithm>

InferenceWorker::InferenceWorker(std::shared_ptr<TrtInference> trt,
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> queue,
    QObject* parent)
    : QObject(parent)
    , m_trt(std::move(trt))
    , m_queue(std::move(queue))
{
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

        if (packet.inputTensor.empty()) {
            continue;
        }

        std::vector<float> output(m_trt->getOutputElementCount(), 0.0f);
        if (!m_trt->doInference(packet.inputTensor.data(), output.data())) {
            emit inferenceError("TensorRT inference failed.");
            continue;
        }

        QImage image = convertOutputToImage(output, packet.outputWidth, packet.outputHeight, packet.outputChannels);
        if (!image.isNull()) {
            emit resultReady(image);
        }
    }
}

void InferenceWorker::stop() {
    m_stop.store(true);
    if (m_queue) {
        m_queue->stop();
    }
}

QImage InferenceWorker::convertOutputToImage(const std::vector<float>& output, int width, int height, int channels) const {
    if (width <= 0 || height <= 0 || channels <= 0) {
        return QImage();
    }

    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return image;
    }

    int planeSize = width * height;
    for (int y = 0; y < height; ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            auto clamp = [](float v) {
                return static_cast<uchar>(std::min(255.0f, std::max(0.0f, v * 255.0f)));
            };
            float r = output[idx];
            float g = channels > 1 ? output[planeSize + idx] : r;
            float b = channels > 2 ? output[2 * planeSize + idx] : r;
            row[x * 3 + 0] = clamp(r);
            row[x * 3 + 1] = clamp(g);
            row[x * 3 + 2] = clamp(b);
        }
    }
    return image;
}
