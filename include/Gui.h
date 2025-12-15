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

    // 核心功能
    void start();
    void stoprecord();
    void closeEvent(QCloseEvent* event) override;

    // 预览线程
    void updateDVS();
    void updateRGB();

    // 线程对象
    std::thread DVS_thread;
    std::thread RGB_thread;

    // 状态控制
    bool is_running;
    std::mutex mutex;

private:
    // UI 组件
    QLabel* view_DVS;
    QLabel* view_RGB;

    QHBoxLayout* buttonLayout;
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QHBoxLayout* datasetLayout;

    QLineEdit* datasetInput;

    // 硬件设备
    DVS dvs;
    RGB rgb_left;  // 左相机
    RGB rgb_right; // 右相机
    UNO uno;
};

#endif // GUI_H