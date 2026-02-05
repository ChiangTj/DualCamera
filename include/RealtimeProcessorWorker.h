#ifndef REALTIMEPROCESSORWORKER_H
#define REALTIMEPROCESSORWORKER_H

#include <QObject>

#include <atomic>
#include <memory>

#include "DataProcessor.h"
#include "LatestFrameQueue.h"
#include "RealtimeTypes.h"

class RealtimeProcessorWorker : public QObject {
    Q_OBJECT

public:
    RealtimeProcessorWorker(const cv::Mat& homographyMatrix,
        std::shared_ptr<LatestFrameQueue<RealtimeFrameInput>> inputQueue,
        std::shared_ptr<LatestFrameQueue<PreprocessPacket>> inferenceQueue,
        QObject* parent = nullptr);

public slots:
    void process();
    void stop();

signals:
    void preprocessError(const QString& message);

private:
    std::unique_ptr<DataProcessor> m_processor;
    std::shared_ptr<LatestFrameQueue<RealtimeFrameInput>> m_inputQueue;
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> m_inferenceQueue;
    std::atomic<bool> m_stop{ false };
};

#endif // REALTIMEPROCESSORWORKER_H
