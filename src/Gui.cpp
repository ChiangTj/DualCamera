#include "Gui.h" // 假设你的 .h 文件在 include 目录
#include <QDir>         // 包含 QDir (用于创建文件夹)

// 构造函数
GUI::GUI(QWidget* parent) : QMainWindow(parent) {
    // 1. 基本设置
    is_running = false;
    setWindowTitle("DualCamera");
    resize(QSize(1920, 920));
    QFont font;
    font.setPointSize(15);
    setFont(font);

    // 2. 布局和控件
    buttonLayout = new QHBoxLayout();
    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();

    // DVS 和 RGB 的显示 QLabel
    view_DVS = new QLabel("DVS Feed (Waiting...)");
    view_RGB = new QLabel("RGB Feed (Waiting...)");

    // *** 建议的修改 ***
    // 设置最小尺寸，防止它们在启动时不可见
    view_DVS->setMinimumSize(640, 540);
    view_RGB->setMinimumSize(640, 540);
    // 设置 QLabel 自动缩放其内容 (比在循环中手动缩放更高效)
    view_DVS->setScaledContents(true);
    view_RGB->setScaledContents(true);
    // (可选) 添加一个边框，使它们更明显
    view_DVS->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    view_RGB->setFrameStyle(QFrame::Panel | QFrame::Sunken);


    // 数据集输入框
    QLabel* datasetLabel = new QLabel(tr("Dataset Name:"));
    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name");
    datasetLayout->addWidget(datasetLabel);
    datasetLayout->addWidget(datasetInput);

    // 按钮
    auto openCameraButton = new QPushButton(tr("Open Camera (Start)"));
    auto stopButton = new QPushButton(tr("Stop Record"));
    stopButton->setCheckable(true); // (这个 setCheckable 似乎没有必要)

    // 3. 组合布局
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

    // 4. 连接按钮信号
    connect(openCameraButton, &QPushButton::clicked, this, &GUI::start);
    connect(stopButton, &QPushButton::clicked, this, &GUI::stoprecord);

    // 5. *** 核心修复：初始化定时器 ***
    m_dvs_display_timer = new QTimer(this);
    m_rgb_display_timer = new QTimer(this);

    // 6. *** 核心修复：连接定时器到槽函数 ***
    connect(m_dvs_display_timer, &QTimer::timeout, this, &GUI::updateDvsDisplaySlot);
    connect(m_rgb_display_timer, &QTimer::timeout, this, &GUI::updateRgbDisplaySlot);
}

// 析构函数
GUI::~GUI() {
    // 确保在退出时停止所有操作
    if (is_running) {
        stoprecord();
    }
}

// 启动录制
void GUI::start() {
    // 创建数据集文件夹
    std::string dataset_name = datasetInput->text().toStdString();
    if (dataset_name.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter a dataset name!");
        return;
    }
    std::string folder_path = "./" + dataset_name;
    QDir().mkpath(QString::fromStdString(folder_path));

    // (锁可用于保护 is_running 标志)
    std::lock_guard<std::mutex> lock(mutex);

    // is_running为程序运行标志
    if (!is_running) {
        is_running = true;

        // 启动后端采集线程 (这些 .start() 应该是非阻塞的)
        dvs.start(dataset_name);       // dvs开始录制
        rgb.startCapture(folder_path); // RGB开始录制
        uno.start();                   // 单片机开始输出方波控制信号

        // *** 核心修复：不再创建 std::thread，而是启动 QTimer ***
        // 刷新率：DVS 可以非常快 (例如 100 FPS)
        // RGB 匹配相机帧率 (例如 30 FPS)
        m_dvs_display_timer->start(10); // 10ms
        m_rgb_display_timer->start(33); // 33ms (~30 FPS)

        qDebug() << "Capture started. GUI timers running.";
    }
}

// 停止录制
void GUI::stoprecord() {
    std::lock_guard<std::mutex> lock(mutex);

    if (is_running) {
        is_running = false; // 立即设置标志，停止所有循环

        // *** 核心修复：停止定时器 ***
        m_dvs_display_timer->stop();
        m_rgb_display_timer->stop();

        // 停止后端 (这些 .stop() 应该是阻塞的，等待线程退出)
        uno.stop();
        dvs.stopRecord();
        rgb.stopCapture();

        qDebug() << "Capture stopped. GUI timers stopped.";
        view_DVS->setText("DVS Feed (Stopped)");
        view_RGB->setText("RGB Feed (Stopped)");
    }
}

// *** 核心修复：DVS 更新槽 (在 GUI 线程中运行) ***
void GUI::updateDvsDisplaySlot() {
    if (!is_running) return; // 检查标志

    cv::Mat temp;
    temp = dvs.getFrame(); // 从 DVS 后端获取帧

    if (temp.empty()) {
        return; // 没有新帧
    }

    cv::Mat frame;
    // 注意：dsize 硬编码不是一个好习惯，但我们暂时遵循你的代码
    cv::Size dsize = cv::Size(640, 540);
    cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);

    // 假设 DVS 帧已经是 RGB888 格式 (根据你的原始代码)
    // *** 修复警告：frame.step 是 size_t，QImage 需要 int ***
    auto qimg = QImage(frame.data,
        frame.cols,
        frame.rows,
        static_cast<int>(frame.step), // <-- 修复警告
        QImage::Format_RGB888);

    // QPixmap::fromImage 会复制数据，所以 qimg 是安全的
    view_DVS->setPixmap(QPixmap::fromImage(qimg));
}


// *** 核心修复：RGB 更新槽 (在 GUI 线程中运行) ***
void GUI::updateRgbDisplaySlot() {
    if (!is_running) return; // 检查标志

    cv::Mat temp_bgr_frame; // 变量名明确指出这是 BGR
    rgb.getLatestFrame(&temp_bgr_frame); // 从 RGB 后端获取帧 (BGR 格式)

    if (temp_bgr_frame.empty()) {
        return; // 没有新帧
    }

    cv::Mat resized_bgr_frame;
    cv::Mat display_rgb_frame;
    cv::Size dsize = cv::Size(640, 540);
    cv::resize(temp_bgr_frame, resized_bgr_frame, dsize, 0, 0, cv::INTER_AREA);

    // *** 修复颜色：将 BGR 转换为 RGB ***
    cv::cvtColor(resized_bgr_frame, display_rgb_frame, cv::COLOR_BGR2RGB);

    // *** 修复警告：frame.step 是 size_t，QImage 需要 int ***
    auto qimg = QImage(display_rgb_frame.data,
        display_rgb_frame.cols,
        display_rgb_frame.rows,
        static_cast<int>(display_rgb_frame.step), // <-- 修复警告
        QImage::Format_RGB888); // 格式现在是 RGB888

    view_RGB->setPixmap(QPixmap::fromImage(qimg));
}


// 关闭窗口事件
void GUI::closeEvent(QCloseEvent* event) {
    if (is_running) {
        stoprecord(); // 确保在关闭窗口时停止所有操作
    }
    event->accept(); // 接受关闭事件，正常退出
    // _exit(0); // 避免使用 _exit(0)，它会强制终止
}