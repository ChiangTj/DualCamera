#ifndef GUI_H
#define GUI_H

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaType>
#include <QObject>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

#include "../include/DataProcessor.h"
#include "../include/DVS.h"
#include "../include/InferenceWorker.h"
#include "../include/RGB.h"
#include "../include/RealtimeProcessorWorker.h"
#include "../include/TrtInference.h"
#include "../include/Uno.h"

Q_DECLARE_METATYPE(cv::Mat)

class GUI : public QMainWindow {
    Q_OBJECT

public:
    GUI(QWidget* parent = nullptr);
    ~GUI();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onRecordButtonClicked();
    void onProcessButtonClicked();
    void onPlaybackButtonClicked();
    void onSliderMoved(int frame_index);

    void updateLivePreview();
    void updatePlayback();

    void onProcessingFinished(bool success);
    void onProcessingProgress(const QString& message);
    void onBlurryFrameReady(const cv::Mat& frame);
    void onDeblurredImageReady(int frameIndex, const QImage& image);
    void onInferenceStreamFinished(int expectedFrameCount);

private:
    enum class AppState {
        Idle,
        Recording,
        Processing,
        Playback_Paused,
        Playback_Playing
    };

    void setUiState(AppState newState);
    void setupUi();
    void startRecording();
    void stopRecording();
    void setupPlayback();
    void showFrame(int index);
    void tryEnterPlaybackState();
    void setupRealtimeProcessing();
    void stopRealtimeProcessing();
    void handleRealtimeRgbFrame(cv::Mat frame, unsigned int frameNumber);
    bool loadHomography(const QString& path);

    QVBoxLayout* mainLayout = nullptr;
    QHBoxLayout* viewLayout = nullptr;
    QHBoxLayout* datasetLayout = nullptr;
    QHBoxLayout* buttonLayout = nullptr;

    QLabel* view_RGB = nullptr;
    QLabel* view_Deblurred = nullptr;
    QLineEdit* datasetInput = nullptr;
    QPushButton* recordButton = nullptr;
    QPushButton* processButton = nullptr;
    QPushButton* playbackButton = nullptr;
    QSlider* playbackSlider = nullptr;

    DVS dvs;
    RGB rgb;
    UNO uno;

    AppState m_currentState = AppState::Idle;
    QString m_currentSegmentPath;
    int m_segmentCounter = 0;

    cv::Mat m_homographyMatrix;

    QThread* m_processThread = nullptr;

    std::shared_ptr<TrtInference> m_trtEngine;
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> m_inferenceQueue;
    InferenceWorker* m_inferenceWorker = nullptr;
    QThread* m_inferenceThread = nullptr;
    bool m_trtReady = false;

    std::shared_ptr<LatestFrameQueue<RealtimeFrameInput>> m_realtimeQueue;
    RealtimeProcessorWorker* m_realtimeWorker = nullptr;
    QThread* m_realtimeThread = nullptr;
    std::mutex m_realtimePairMutex;
    std::deque<std::pair<cv::Mat, unsigned int>> m_pendingRealtimeRgbFrames;

    QTimer* m_livePreviewTimer = nullptr;
    QTimer* m_playbackTimer = nullptr;

    int m_playbackIndex = 0;
    std::vector<cv::Mat> m_blurryFrames;
    std::vector<cv::Mat> m_deblurredFrames;

    bool m_processingCompleted = false;
    bool m_inferenceCompleted = false;
    int m_expectedFrameCount = 0;
};

#endif // GUI_H
