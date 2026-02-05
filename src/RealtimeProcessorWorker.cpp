#include "../include/RealtimeProcessorWorker.h"

RealtimeProcessorWorker::RealtimeProcessorWorker(
    const cv::Mat& homographyMatrix,
    std::shared_ptr<LatestFrameQueue<RealtimeFrameInput>> inputQueue,
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> inferenceQueue,
    QObject* parent)
    : QObject(parent)
    , m_processor(std::make_unique<DataProcessor>("", homographyMatrix))
    , m_inputQueue(std::move(inputQueue))
    , m_inferenceQueue(std::move(inferenceQueue)) {
    m_processor->setMode(DataProcessor::Mode::Realtime);
    m_processor->setRealtimeOutputConfig(1000, 720, 1000, 720, 6, 280);
}

void RealtimeProcessorWorker::process() {
    if (!m_processor || !m_inputQueue || !m_inferenceQueue) {
        emit preprocessError("Realtime processor is not initialized.");
        return;
    }

    while (!m_stop.load()) {
        RealtimeFrameInput input;
        if (!m_inputQueue->waitPop(input)) {
            break;
        }

        if (input.rgbFrame.empty()) {
            continue;
        }

        DataProcessor::RealtimeOutput output;
        if (!m_processor->processRealtimeFrame(
            input.rgbFrame,
            input.eventWindow.events,
            input.eventWindow.startTimestamp,
            input.eventWindow.endTimestamp,
            output)) {
            emit preprocessError("Realtime preprocessing failed.");
            continue;
        }

        PreprocessPacket packet;
        packet.frameIndex = -1;
        packet.outputWidth = output.alignedRgb.cols;
        packet.outputHeight = output.alignedRgb.rows;
        packet.outputChannels = output.alignedRgb.channels();
        packet.rgbTensor = std::move(output.rgbNchw);
        packet.voxelTensor = std::move(output.voxelGrid);

        m_inferenceQueue->pushReplacingOldest(std::move(packet));
    }
}

void RealtimeProcessorWorker::stop() {
    m_stop.store(true);
    if (m_inputQueue) {
        m_inputQueue->stop();
    }
}
