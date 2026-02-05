#ifndef INFERENCEWORKER_H
#define INFERENCEWORKER_H

#include <QObject>
#include <QImage>

#include <atomic>
#include <memory>
#include <vector>

#include "LatestFrameQueue.h"
#include "TrtInference.h"

struct PreprocessPacket {
    std::vector<float> inputTensor;
    int outputWidth = 0;
    int outputHeight = 0;
    int outputChannels = 0;
};

class InferenceWorker : public QObject {
    Q_OBJECT

public:
    explicit InferenceWorker(std::shared_ptr<TrtInference> trt,
        std::shared_ptr<LatestFrameQueue<PreprocessPacket>> queue,
        QObject* parent = nullptr);

public slots:
    void process();
    void stop();

signals:
    void resultReady(const QImage& image);
    void inferenceError(const QString& message);

private:
    QImage convertOutputToImage(const std::vector<float>& output, int width, int height, int channels) const;

    std::shared_ptr<TrtInference> m_trt;
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> m_queue;
    std::atomic<bool> m_stop{ false };
};

#endif // INFERENCEWORKER_H
