#include "../include/Gui.h"
#include <QMessageBox>
#include <QDebug>

// =============================================
// 构造函数
// =============================================
GUI::GUI(QWidget* parent)
    : QMainWindow(parent),
    // 初始化列表：分别初始化两台相机
    // 参数：(CameraIndex, Name, DefaultPath)
    rgb_left(0, "left", "./"),
    rgb_right(1, "right", "./")
{
    is_running = false;
    setWindowTitle("DualCamera Recorder");
    resize(QSize(1280, 720)); // 调整默认窗口大小

    // 设置全局字体
    QFont font;
    font.setPointSize(12);
    setFont(font);

    // --- 初始化布局与控件 ---
    buttonLayout = new QHBoxLayout();
    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();

    // 预览窗口
    view_DVS = new QLabel();
    view_RGB = new QLabel();

    // 设置背景色和边框，方便区分
    view_DVS->setStyleSheet("QLabel { background-color : black; border : 1px solid gray; }");
    view_RGB->setStyleSheet("QLabel { background-color : black; border : 1px solid gray; }");
    view_DVS->setAlignment(Qt::AlignCenter);
    view_RGB->setAlignment(Qt::AlignCenter);

    // 默认显示文字
    view_DVS->setText("<font color='white'>DVS View</font>");
    view_RGB->setText("<font color='white'>Left RGB View</font>");

    // 数据集输入区域
    QLabel* datasetLabel = new QLabel(tr("Dataset Name:"));
    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name (e.g. experiment_01)");
    datasetLayout->addWidget(datasetLabel);
    datasetLayout->addWidget(datasetInput);

    // 按钮区域
    auto openCameraButton = new QPushButton(tr("Start Recording"));
    auto stopButton = new QPushButton(tr("Stop Recording"));
    stopButton->setCheckable(true); // 让停止按钮可以保持按下状态（可选）

    buttonLayout->addWidget(openCameraButton);
    buttonLayout->addWidget(stopButton);

    // 组装主界面
    viewLayout->addWidget(view_DVS);
    viewLayout->addWidget(view_RGB);

    mainLayout->addLayout(viewLayout);
    mainLayout->addLayout(datasetLayout);
    mainLayout->addLayout(buttonLayout);

    auto mainWidget = new QWidget();
    mainWidget->setLayout(mainLayout);
    setCentralWidget(mainWidget);

    // --- 信号槽连接 ---
    connect(openCameraButton, &QPushButton::clicked, this, &GUI::start);
    connect(stopButton, &QPushButton::clicked, this, &GUI::stoprecord);
}

GUI::~GUI() {
    stoprecord(); // 析构时确保停止
}

// =============================================
// 开始录制
// =============================================
void GUI::start() {
    // 1. 获取并检查数据集名称
    std::string dataset_name = datasetInput->text().toStdString();
    if (dataset_name.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter a dataset name!");
        return;
    }

    // 2. 准备文件路径
    std::string folder_path = "./" + dataset_name;
    std::string left_folder = folder_path + "/left";
    std::string right_folder = folder_path + "/right";

    // 创建文件夹
    QDir().mkpath(QString::fromStdString(left_folder));
    QDir().mkpath(QString::fromStdString(right_folder));

    std::lock_guard<std::mutex> lock(mutex);
    if (!is_running) {
        is_running = true;

        std::cout << "[GUI] Starting devices..." << std::endl;

        // 3. 启动 RGB 相机
        rgb_left.startCapture(left_folder);
        rgb_right.startCapture(right_folder);

        // [可选] 设置相机参数 (曝光时间 us)
        // rgb_left.setExposureTime(10000);  // 左相机 10ms
        // rgb_right.setExposureTime(5000);  // 右相机 5ms

        // 4. 启动其他设备
        dvs.start(folder_path, "events");
        uno.start();

        // 5. 启动预览线程
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
        is_running = false; // 标志位置 false，通知预览线程退出

        std::cout << "[GUI] Stopping recording..." << std::endl;

        // 停止所有设备
        rgb_left.stopCapture();
        rgb_right.stopCapture();
        dvs.stopRecord();
        uno.stop();

        // 恢复 UI 显示
        QMetaObject::invokeMethod(view_DVS, [this]() {
            view_DVS->clear();
            view_DVS->setText("<font color='white'>Stopped</font>");
            });
        QMetaObject::invokeMethod(view_RGB, [this]() {
            view_RGB->clear();
            view_RGB->setText("<font color='white'>Stopped</font>");
            });

        std::cout << "[GUI] Recording finished." << std::endl;
    }
}

// =============================================
// 预览线程：DVS
// =============================================
void GUI::updateDVS() {
    cv::Size dsize = cv::Size(640, 480); // 调整预览分辨率
    cv::Mat temp, frame;

    while (is_running) {
        temp = dvs.getFrame();
        if (!temp.empty()) {
            cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);

            // DVS 通常输出 RGB 或 Gray，假设这里是 RGB 格式可以直接显示
            // 务必确保 temp 的数据格式正确
            auto qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

            // 使用 invokeMethod 确保在主线程更新 UI (线程安全)
            QMetaObject::invokeMethod(view_DVS, [this, qimg]() {
                view_DVS->setPixmap(QPixmap::fromImage(qimg).scaled(view_DVS->size(), Qt::KeepAspectRatio));
                });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// =============================================
// 预览线程：RGB (Left Camera)
// =============================================
void GUI::updateRGB() {
    cv::Mat temp;
    cv::Size dsize = cv::Size(640, 480); // 预览分辨率

    while (is_running) {
        // 获取左相机的最新帧
        rgb_left.getLatestFrame(&temp);

        if (!temp.empty()) {
            cv::Mat frame;
            cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);

            // 【关键】OpenCV 是 BGR 格式，Qt 是 RGB 格式，需要转换颜色空间
            // 否则显示出来的图片会偏蓝
            cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

            // 构造 QImage (注意：frame.data 必须在 setPixmap 前有效，这里因为是局部变量且立即 copy 到 pixmap 所以没问题)
            auto qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

            // 线程安全更新 UI
            QMetaObject::invokeMethod(view_RGB, [this, qimg]() {
                view_RGB->setPixmap(QPixmap::fromImage(qimg).scaled(view_RGB->size(), Qt::KeepAspectRatio));
                });
        }

        // 控制刷新率 (~30fps)
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

void GUI::closeEvent(QCloseEvent* event) {
    stoprecord();
    event->accept();
}