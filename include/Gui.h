#ifndef GUI_H
#define GUI_H

#include <QMainWindow>
#include <QObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>
#include <QCloseEvent>
#include <QDir>
#include <QProcess>
#include <QTimer>
#include <QThread> // [新增]

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <atomic>
#include <memory>

// 包含您的后端模块
#include "../include/RGB.h"
#include "../include/DVS.h"
#include "../include/Uno.h"
#include "../include/DataProcessor.h" // [新增] DataProcessor

class GUI : public QMainWindow {
    Q_OBJECT

public:
    GUI(QWidget* parent = nullptr);
    ~GUI();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // --- 1. UI 按钮槽 ---
    void onRecordButtonClicked();
    void onProcessButtonClicked();
    void onPlaybackButtonClicked();
    void onSliderMoved(int frame_index);

    // --- 2. 状态更新槽 ---
    void updateLivePreview();
    void updatePlayback();

    // --- 3. C++ DataProcessor 槽 [新增] ---
    void onProcessingFinished(bool success);
    void onProcessingProgress(const QString& message);

    // --- 4. Python 进程槽 ---
    void onPythonOutput();
    void onPythonError();
    void onPythonFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    // --- UI 状态 ---
    enum class AppState {
        Idle,
        Recording,
        Processing,      // C++ 处理中
        Inference,       // Python 推理中
        Playback_Paused,
        Playback_Playing
    };
    void setUiState(AppState newState);
    AppState m_currentState;

    // --- UI 控件 ---
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QHBoxLayout* datasetLayout;
    QHBoxLayout* buttonLayout;

    QLabel* view_RGB;
    QLabel* view_Deblurred;
    QLineEdit* datasetInput;
    QPushButton* recordButton;
    QPushButton* processButton;
    QPushButton* playbackButton;
    QSlider* playbackSlider;

    // --- 后端模块 ---
    DVS dvs;
    RGB rgb;
    UNO uno;

    // --- 状态和数据 ---
    QString m_currentSegmentPath;
    int m_segmentCounter;

    // [新增] 单应性矩阵
    cv::Mat m_homographyMatrix;
    bool loadHomography(const QString& path);

    // --- 处理线程 ---
    QThread* m_processThread = nullptr; // [新增]
    QProcess* m_pythonProcess;

    // --- 预览和回放 ---
    QTimer* m_livePreviewTimer;
    QTimer* m_playbackTimer;

    int m_playbackIndex;
    std::vector<cv::Mat> m_blurryFrames;
    std::vector<cv::Mat> m_deblurredFrames;

    // --- 辅助函数 ---
    void setupUi();
    void startRecording();
    void stopRecording();

    // [修改] 不再直接由按钮调用，而是由 C++ 结束后自动调用
    void launchPythonInference();

    void setupPlayback(const QString& segmentPath);
    void showFrame(int index);
};

#endif // GUI_H