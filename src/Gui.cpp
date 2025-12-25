#include "../include/Gui.h"
#include <QMessageBox>
#include <QDebug>
#include <QSizePolicy>
#include <QDir>

GUI::GUI(QWidget* parent)
    : QMainWindow(parent),
    rgb_left(0, "left", "./"),
    rgb_right(1, "right", "./")
{
    is_running = false;
    setWindowTitle("DualCamera Recorder");
    resize(QSize(1280, 720));

    buttonLayout = new QHBoxLayout();
    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();

    view_DVS = new QLabel("DVS Feed");
    view_RGB = new QLabel("RGB Feed (Left)");

    view_DVS->setStyleSheet("QLabel { background-color : black; border : 1px solid gray; }");
    view_RGB->setStyleSheet("QLabel { background-color : black; border : 1px solid gray; }");
    view_DVS->setAlignment(Qt::AlignCenter);
    view_RGB->setAlignment(Qt::AlignCenter);
    view_DVS->setScaledContents(true);
    view_RGB->setScaledContents(true);
    view_DVS->setMinimumSize(320, 240);
    view_RGB->setMinimumSize(320, 240);

    QLabel* datasetLabel = new QLabel(tr("Dataset Name:"));
    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name");
    datasetLayout->addWidget(datasetLabel);
    datasetLayout->addWidget(datasetInput);

    auto openCameraButton = new QPushButton(tr("Start Recording"));
    auto stopButton = new QPushButton(tr("Stop Recording"));
    stopButton->setCheckable(true);

    buttonLayout->addWidget(openCameraButton);
    buttonLayout->addWidget(stopButton);
    viewLayout->addWidget(view_DVS);
    viewLayout->addWidget(view_RGB);

    mainLayout->addLayout(viewLayout);
    mainLayout->addLayout(datasetLayout);
    mainLayout->addLayout(buttonLayout);

    auto mainWidget = new QWidget();
    mainWidget->setLayout(mainLayout);
    setCentralWidget(mainWidget);

    connect(openCameraButton, &QPushButton::clicked, this, &GUI::start);
    connect(stopButton, &QPushButton::clicked, this, &GUI::stoprecord);

    // 初始化定时器
    m_dvs_display_timer = new QTimer(this);
    m_rgb_display_timer = new QTimer(this);

    connect(m_dvs_display_timer, &QTimer::timeout, this, &GUI::updateDvsDisplaySlot);
    connect(m_rgb_display_timer, &QTimer::timeout, this, &GUI::updateRgbDisplaySlot);
}

GUI::~GUI() {
    stoprecord();
}

void GUI::start() {
    std::string dataset_name = datasetInput->text().toStdString();
    if (dataset_name.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter a dataset name!");
        return;
    }

    std::string folder_path = "./" + dataset_name;
    std::string left_folder = folder_path + "/left";
    std::string right_folder = folder_path + "/right";

    QDir().mkpath(QString::fromStdString(left_folder));
    QDir().mkpath(QString::fromStdString(right_folder));

    std::lock_guard<std::mutex> lock(mutex);
    if (!is_running) {
        is_running = true;

        // 1. 启动硬件
        rgb_left.startCapture(left_folder);
        rgb_right.startCapture(right_folder);
        dvs.start(folder_path, dataset_name);
        uno.start();

        // 2. 启动 GUI 刷新 (33ms ~ 30FPS)
        m_dvs_display_timer->start(33);
        m_rgb_display_timer->start(33);

        qDebug() << "Capture started.";
    }
}

void GUI::stoprecord() {
    std::lock_guard<std::mutex> lock(mutex);
    if (is_running) {
        is_running = false;

        // 1. 停止定时器
        m_dvs_display_timer->stop();
        m_rgb_display_timer->stop();

        // 2. 停止硬件 (耗时操作，界面可能会短暂卡顿，属正常)
        uno.stop();
        dvs.stopRecord();
        rgb_left.stopCapture();
        rgb_right.stopCapture();

        view_DVS->setText("Stopped");
        view_RGB->setText("Stopped");
        qDebug() << "Capture stopped.";
    }
}

void GUI::updateDvsDisplaySlot() {
    if (!is_running) return;

    cv::Mat temp = dvs.getFrame(); // 非阻塞获取
    if (temp.empty()) return;

    cv::Mat frame;
    cv::resize(temp, frame, cv::Size(640, 480));

    // Qt 主线程中直接操作 UI，无需 Invoke
    QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    view_DVS->setPixmap(QPixmap::fromImage(qimg));
}

void GUI::updateRgbDisplaySlot() {
    if (!is_running) return;

    cv::Mat temp;
    // 默认预览左相机
    rgb_left.getLatestFrame(&temp);

    if (temp.empty()) return;

    cv::Mat frame;
    cv::resize(temp, frame, cv::Size(640, 480));
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

    QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(qimg));
}

void GUI::closeEvent(QCloseEvent* event) {
    stoprecord();
    event->accept();
}