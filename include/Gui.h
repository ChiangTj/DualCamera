#ifndef GUI_H
#define GUI_H

#include <chrono>
#include <thread>
#include <QLabel>
#include <QMainWindow>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QPushButton>
#include <QCloseEvent>
#include <QApplication>
#include <opencv2/opencv.hpp>
#include <QLineEdit>
#include <QMessageBox>
#include <mutex>

// 包含设备头文件
#include "RGB.h"
#include "DVS.h"
#include "Uno.h"

class GUI : public QMainWindow {
public:
    GUI(QWidget* parent = nullptr);
    ~GUI();

    // 核心功能函数
    void start();        // 开始录制
    void stoprecord();   // 停止录制
    void closeEvent(QCloseEvent* event) override;

    // 显示线程函数
    void updateDVS();    // 刷新 DVS 预览
    void updateRGB();    // 刷新 RGB 预览 (默认显示 Left 相机)

    // 线程对象
    std::thread DVS_thread;
    std::thread RGB_thread;

    // 运行状态控制
    bool is_running;
    std::mutex mutex;

private:
    // --- UI 组件 ---
    QLabel* view_DVS;
    QLabel* view_RGB;    // 用于显示 RGB 画面（预览左相机）

    QHBoxLayout* buttonLayout;
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QHBoxLayout* datasetLayout;

    QLineEdit* datasetInput; // 数据集名称输入框

    // --- 硬件设备对象 ---
    DVS dvs;             // Prophesee DVS
    RGB rgb_left;        // 左海康相机 (Index 0)
    RGB rgb_right;       // 右海康相机 (Index 1)
    UNO uno;             // 信号发生器
};

#endif // GUI_H