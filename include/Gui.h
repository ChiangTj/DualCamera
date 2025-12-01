//#ifndef GUI_H
//#define GUI_H
//
//#include <chrono>
//#include <thread>
//#include <QLabel>
//#include <QMainWindow>
//#include <QHBoxLayout>
//#include <QVBoxLayout>
//#include <QDateTime>
//#include <QDebug>
//#include <QImage>
//#include <QPushButton>
//#include <QCloseEvent>
//#include <QApplication>
//#include <opencv2/opencv.hpp>
//#include "RGB.h"
//#include "DVS.h"
//#include "Uno.h"
//#include <QLineEdit>
//#include <QMessageBox>
//
//class GUI : public QMainWindow {
//
//public:
//	
//	GUI(QWidget* parent = nullptr);
//	~GUI();
//	void stoprecord();
//	void updateDVS();
//	void updateRGB();
//	void start();
//	void closeEvent(QCloseEvent* event) override;
//
//	std::thread DVS_thread;
//	std::thread RGB_thread;
//	bool is_running;
//	std::mutex mutex;
//
//private:
//	QLabel* view_DVS;
//	QLabel* view_RGB;
//	QHBoxLayout* buttonLayout;
//	QVBoxLayout* mainLayout;
//	QHBoxLayout* viewLayout;
//	QLineEdit* datasetInput;
//	QHBoxLayout* datasetLayout;
//	DVS dvs;
//	RGB rgb;
//	UNO uno;
//};
//
//#endif // !GUI_H

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
#include "RGB.h"
#include "DVS.h"
#include "Uno.h"
#include <QLineEdit>
#include <QMessageBox>
#include <mutex>

class GUI : public QMainWindow {
public:
    GUI(QWidget* parent = nullptr);
    ~GUI();

    void stoprecord();
    void updateDVS();
    void updateRGB();
    void start();
    void closeEvent(QCloseEvent* event) override;

    // 线程
    std::thread DVS_thread;
    std::thread RGB_thread;
    bool is_running;
    std::mutex mutex;

private:
    // 显示控件
    QLabel* view_DVS;
    QLabel* view_RGB;  // 显示其中一台相机（左相机）

    // 布局
    QHBoxLayout* buttonLayout;
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QHBoxLayout* datasetLayout;

    // 数据集输入
    QLineEdit* datasetInput;

    // 设备对象
    DVS dvs;
    RGB rgb_left;
    RGB rgb_right;
    UNO uno;
};

#endif // GUI_H
