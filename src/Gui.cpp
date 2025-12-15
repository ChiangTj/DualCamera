#include "../include/Gui.h"
#include <QMessageBox>
#include <QDebug>
#include <QSizePolicy> // 关键：解决窗口自动放大

// =============================================
// 构造函数
// =============================================
GUI::GUI(QWidget* parent)
    : QMainWindow(parent),
    rgb_left(0, "left", "./"),   // 初始化左相机 (Index 0)
    rgb_right(1, "right", "./")  // 初始化右相机 (Index 1)
{
    is_running = false;
    setWindowTitle("DualCamera Recorder");
    resize(QSize(1280, 720));

    QFont font;
    font.setPointSize(12);
    setFont(font);

    // --- UI 初始化 ---
    buttonLayout = new QHBoxLayout();
    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();

    view_DVS = new QLabel();
    view_RGB = new QLabel();

    // 样式设置
    view_DVS->setStyleSheet("QLabel { background-color : black; border : 1px solid gray; }");
    view_RGB->setStyleSheet("QLabel { background-color : black; border : 1px solid gray; }");
    view_DVS->setAlignment(Qt::AlignCenter);
    view_RGB->setAlignment(Qt::AlignCenter);

    // 【关键修复 1】设置尺寸策略为 Ignored，防止 setPixmap 撑大窗口
    view_DVS->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    view_RGB->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 设置最小尺寸
    view_DVS->setMinimumSize(320, 240);
    view_RGB->setMinimumSize(320, 240);

    // 数据集输入
    QLabel* datasetLabel = new QLabel(tr("Dataset Name:"));
    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name");
    datasetLayout->addWidget(datasetLabel);
    datasetLayout->addWidget(datasetInput);

    // 按钮
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
}

GUI::~GUI() {
    stoprecord();
}

// =============================================
// 开始录制
// =============================================
void GUI::start() {
    std::string dataset_name = datasetInput->text().toStdString();
    if (dataset_name.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter a dataset name!");
        return;
    }

    // 路径准备
    std::string folder_path = "./" + dataset_name;
    std::string left_folder = folder_path + "/left";
    std::string right_folder = folder_path + "/right";

    QDir().mkpath(QString::fromStdString(left_folder));
    QDir().mkpath(QString::fromStdString(right_folder));

    std::lock_guard<std::mutex> lock(mutex);
    if (!is_running) {
        is_running = true;

        std::cout << "[GUI] Starting devices..." << std::endl;

        // 1. 启动 RGB 相机
        rgb_left.startCapture(left_folder);
        rgb_right.startCapture(right_folder);

        // 可选：设置参数
        // rgb_left.setExposureTime(10000);
        // rgb_right.setExposureTime(5000);

        // 2. 启动 DVS (适配新的 start 接口)
        // 将生成 ./dataset_name/events.raw
        dvs.start(folder_path, "events");

        // 3. 启动触发器
        uno.start();

        // 4. 启动预览线程
        DVS_thread = std::thread(&GUI::updateDVS, this);
        RGB_thread = std::thread(&GUI::updateRGB, this);

        DVS_thread.detach();
        RGB_thread.detach();

        std::cout << "[GUI] All systems running." << std::endl;
    }
}

// =============================================
// 停止录制
// =============================================
void GUI::stoprecord() {
    std::lock_guard<std::mutex> lock(mutex);
    if (is_running) {
        is_running = false; // 停止预览线程循环

        std::cout << "[GUI] Stopping recording..." << std::endl;

        rgb_left.stopCapture();
        rgb_right.stopCapture();
        dvs.stopRecord();
        uno.stop();

        // 恢复 UI 状态
        QMetaObject::invokeMethod(view_DVS, [this]() {
            view_DVS->clear();
            view_DVS->setText("Stopped");
            });
        QMetaObject::invokeMethod(view_RGB, [this]() {
            view_RGB->clear();
            view_RGB->setText("Stopped");
            });

        std::cout << "[GUI] Recording finished." << std::endl;
    }
}

// =============================================
// DVS 预览线程
// =============================================
void GUI::updateDVS() {
    cv::Size dsize = cv::Size(640, 480);
    cv::Mat temp, frame;

    while (is_running) {
        temp = dvs.getFrame(); // 非阻塞获取
        if (!temp.empty()) {
            cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);

            // 【关键修复 2】深拷贝 (.copy)，防止闪退
            QImage qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888).copy();

            QMetaObject::invokeMethod(view_DVS, [this, qimg]() {
                view_DVS->setPixmap(QPixmap::fromImage(qimg).scaled(view_DVS->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                });
        }

        // 【关键修复 3】降频至 ~30FPS，防止卡顿
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

// =============================================
// RGB 预览线程
// =============================================
void GUI::updateRGB() {
    cv::Mat temp;
    cv::Size dsize = cv::Size(640, 480);

    while (is_running) {
        // 默认显示左相机画面
        rgb_left.getLatestFrame(&temp);

        if (!temp.empty()) {
            cv::Mat frame;
            cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);

            // 颜色空间转换 BGR -> RGB
            cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

            // 【关键修复 2】深拷贝 (.copy)
            QImage qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888).copy();

            QMetaObject::invokeMethod(view_RGB, [this, qimg]() {
                view_RGB->setPixmap(QPixmap::fromImage(qimg).scaled(view_RGB->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                });
        }

        // 【关键修复 3】降频至 ~30FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}
void GUI::closeEvent(QCloseEvent* event) {
    // 窗口关闭时自动停止录制，防止后台残留线程
    stoprecord();
    event->accept();
}