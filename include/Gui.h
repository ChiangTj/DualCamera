#ifndef GUI_H
#define GUI_H

#include <QMainWindow>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer> // 关键
#include <mutex>
#include "RGB.h"
#include "DVS.h"
#include "Uno.h"
#include <QCloseEvent>
#include <QApplication>

class GUI : public QMainWindow {
    Q_OBJECT // 必须添加

public:
    GUI(QWidget* parent = nullptr);
    ~GUI();

    void start();
    void stoprecord();
    void closeEvent(QCloseEvent* event) override;

    bool is_running;
    std::mutex mutex;

private slots:
    // 使用槽函数替代线程循环
    void updateDvsDisplaySlot();
    void updateRgbDisplaySlot();

private:
    QLabel* view_DVS;
    QLabel* view_RGB;
    QHBoxLayout* buttonLayout;
    QVBoxLayout* mainLayout;
    QHBoxLayout* viewLayout;
    QHBoxLayout* datasetLayout;
    QLineEdit* datasetInput;

    // 定时器替代线程
    QTimer* m_dvs_display_timer;
    QTimer* m_rgb_display_timer;

    DVS dvs;
    RGB rgb_left;
    RGB rgb_right;
    UNO uno;
};

#endif // GUI_H