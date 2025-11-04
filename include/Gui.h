#ifndef GUI_H
#define GUI_H

#include <QMainWindow>
#include <QObject>        // 确保 Q_OBJECT 宏可用
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
#include <QProcess>      // +++ ADDED: 用于启动 Python 脚本
#include <QTimer>        // +++ ADDED: 用于实时预览和回放
#include <QMovie>        // (可选) +++ ADDED: 用于显示“处理中”动画

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <atomic>
#include <memory>

// 包含您的后端模块
#include "../include/RGB.h"
#include "../include/DVS.h"
#include "../include/Uno.h" //

class GUI : public QMainWindow {
    Q_OBJECT // <<< 必须添加 Q_OBJECT 宏

public:
    GUI(QWidget* parent = nullptr);
    ~GUI();

protected:
    // 重写关闭事件以安全停止所有线程/进程
    void closeEvent(QCloseEvent* event) override;

private slots:
    // --- 1. UI 按钮槽 ---
    void onRecordButtonClicked();
    void onProcessButtonClicked();
    void onPlaybackButtonClicked();
    void onSliderMoved(int frame_index);

    // --- 2. 状态更新槽 (由 QTimer 驱动) ---
    void updateLivePreview();    // 录制模式: 从 RGB.cpp 获取实时预览
    void updatePlayback();       // 回放模式: 播放内存中的视频帧

    // --- 3. Python 进程 (QProcess) 槽 ---
    void onPythonOutput();       // Python 打印 stdout (用于调试)
    void onPythonError();        // Python 进程出错
    void onPythonFinished(int exitCode, QProcess::ExitStatus exitStatus); // Python 进程结束

private:
    // --- UI 状态 ---
    enum class AppState {
        Idle,           // 空闲，准备录制
        Recording,      // 正在录制
        Processing,     // Python 正在处理
        Playback_Paused, // 回放模式（暂停）
        Playback_Playing // 回放模式（播放）
    };
    void setUiState(AppState newState);
    AppState m_currentState;

    // --- UI 控件 ---
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QHBoxLayout* datasetLayout;
    QHBoxLayout* buttonLayout;

    QLabel* view_RGB;       // 左侧：显示实时预览或录制的模糊视频
    QLabel* view_Deblurred; // 右侧：显示状态或去模糊的清晰视频
    QLineEdit* datasetInput;
    QPushButton* recordButton;   // “录制” / “停止”
    QPushButton* processButton;  // “处理”
    QPushButton* playbackButton; // “播放” / “暂停”
    QSlider* playbackSlider;     // 视频进度条

    QMovie* m_processingMovie; // (可选) "处理中..." 的 GIF 动画

    // --- 后端模块 ---
    DVS dvs;
    RGB rgb;
    UNO uno; //

    // --- 状态和数据 ---
    QString m_currentSegmentPath; // 最近录制的数据段路径 (e.g., "./Dataset1/segment_1")
    int m_segmentCounter;

    // --- Python 进程 ---
    QProcess* m_pythonProcess;

    // --- 预览和回放 ---
    QTimer* m_livePreviewTimer;  // 录制时用于 `updateLivePreview` 的定时器
    QTimer* m_playbackTimer;     // 回放时用于 `updatePlayback` 的定时器

    int m_playbackIndex;         // 当前回放的帧索引
    std::vector<cv::Mat> m_blurryFrames;   // 内存中：HDF5 中的原始帧
    std::vector<cv::Mat> m_deblurredFrames; // 内存中：Python 处理后的 PNG 帧

    // --- 私有辅助函数 ---
    void setupUi(); // 辅助函数：初始化所有 UI 控件

    void startRecording();
    void stopRecording();

    void launchProcessing(); // 启动 QProcess

    void setupPlayback(const QString& segmentPath); // 加载数据到内存
    void showFrame(int index);                      // 在两个 QLabel 中显示第N帧
};

#endif // GUI_H