#ifndef GUI_H
#define GUI_H

#include <chrono>
#include <thread> // (这个可以保留，因为你的后端可能还需要)
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
#include "RGB.h"
#include "DVS.h"
#include "Uno.h"
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer> // +++ 添加 QTimer

class GUI : public QMainWindow {
    // +++ 必须添加 Q_OBJECT 宏！
    Q_OBJECT

public:
    GUI(QWidget* parent = nullptr);
    ~GUI();
    void stoprecord();
    void start();
    void closeEvent(QCloseEvent* event) override;

    // --- 移除 DVS_thread 和 RGB_thread ---
    // std::thread DVS_thread;
    // std::thread RGB_thread;

    bool is_running;
    std::mutex mutex; // (注意：如果只有GUI线程访问，这个mutex可能不再需要)

    // +++ 添加私有槽 ---
private slots:
    void updateDvsDisplaySlot(); // 替代 updateDVS
    void updateRgbDisplaySlot(); // 替代 updateRGB

private:
    // --- 移除线程函数 ---
    // void updateDVS();
    // void updateRGB();

    QLabel* view_DVS;
    QLabel* view_RGB;
    QHBoxLayout* buttonLayout;
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QLineEdit* datasetInput;
    QHBoxLayout* datasetLayout;
    DVS dvs;
    RGB rgb;
    UNO uno;

    // +++ 添加定时器 ---
    QTimer* m_dvs_display_timer;
    QTimer* m_rgb_display_timer;
};

#endif // !GUI_H