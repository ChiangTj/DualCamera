#include "../include/Gui.h" // 确保这是我们新设计的 Gui.h
#include <opencv2/imgproc.hpp> // 用于 cv::cvtColor
#include <QApplication>
#include <QDir>
#include <QMessageBox>

// --- 1. 构造函数：初始化 UI 和状态 ---

GUI::GUI(QWidget* parent)
    : QMainWindow(parent),
    m_currentState(AppState::Idle),
    m_segmentCounter(0),
    m_playbackIndex(0),
    m_pythonProcess(nullptr) // 初始化 QProcess
{
    // 初始化所有 UI 控件
    setupUi();

    // 初始化 Python 进程
    m_pythonProcess = new QProcess(this);

    // 初始化定时器
    m_livePreviewTimer = new QTimer(this);
    m_playbackTimer = new QTimer(this);

    // --- 连接所有信号和槽 ---

    // 1. 按钮
    connect(recordButton, &QPushButton::clicked, this, &GUI::onRecordButtonClicked);
    connect(processButton, &QPushButton::clicked, this, &GUI::onProcessButtonClicked);
    connect(playbackButton, &QPushButton::clicked, this, &GUI::onPlaybackButtonClicked);

    // 2. 滑块
    connect(playbackSlider, &QSlider::sliderMoved, this, &GUI::onSliderMoved);

    // 3. 定时器
    connect(m_livePreviewTimer, &QTimer::timeout, this, &GUI::updateLivePreview);
    connect(m_playbackTimer, &QTimer::timeout, this, &GUI::updatePlayback);

    // 4. Python 进程
    connect(m_pythonProcess, &QProcess::readyReadStandardOutput, this, &GUI::onPythonOutput);
    connect(m_pythonProcess, &QProcess::errorOccurred, this, &GUI::onPythonError);
    connect(m_pythonProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, &GUI::onPythonFinished);

    // 设置 UI 的初始状态
    setUiState(AppState::Idle);
}

GUI::~GUI()
{
    // 确保所有子进程和线程在退出时已停止
    if (m_currentState == AppState::Recording) {
        stopRecording();
    }
    if (m_pythonProcess->state() == QProcess::Running) {
        m_pythonProcess->kill(); // 强制终止 Python
    }
    m_livePreviewTimer->stop();
    m_playbackTimer->stop();
}

// --- 2. UI 辅助函数 (setupUi 和 setUiState) ---

// 辅助函数：创建所有 UI 控件
void GUI::setupUi()
{
    setWindowTitle("双相机采集与处理系统");
    resize(QSize(1600, 700)); // (W, H)

    // 布局
    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();
    buttonLayout = new QHBoxLayout();

    // 视图 (移除了 DVS)
    view_RGB = new QLabel("原始 (RGB)");
    view_Deblurred = new QLabel("去模糊 (结果)");
    view_RGB->setFrameStyle(QFrame::Box);
    view_Deblurred->setFrameStyle(QFrame::Box);
    view_RGB->setMinimumSize(800, 600);
    view_Deblurred->setMinimumSize(800, 600);
    view_RGB->setScaledContents(true);
    view_Deblurred->setScaledContents(true);

    viewLayout->addWidget(view_RGB);
    viewLayout->addWidget(view_Deblurred);

    // 数据集输入
    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("在此输入数据集名称 (例如: 'Test01')");
    datasetLayout->addWidget(new QLabel("数据集名称:"));
    datasetLayout->addWidget(datasetInput);

    // 控制按钮
    recordButton = new QPushButton("Start Recording");
    processButton = new QPushButton("Process Last Segment");
    playbackButton = new QPushButton("Play");
    playbackSlider = new QSlider(Qt::Horizontal);

    buttonLayout->addWidget(recordButton);
    buttonLayout->addWidget(processButton);
    buttonLayout->addSpacing(50);
    buttonLayout->addWidget(playbackButton);
    buttonLayout->addWidget(playbackSlider);

    // 组合
    auto mainWidget = new QWidget();
    mainLayout->addLayout(viewLayout);
    mainLayout->addLayout(datasetLayout);
    mainLayout->addLayout(buttonLayout);
    mainWidget->setLayout(mainLayout);
    setCentralWidget(mainWidget);
}

// 核心：状态机，用于控制 UI 启用/禁用
void GUI::setUiState(AppState newState)
{
    m_currentState = newState;
    switch (m_currentState) {

    case AppState::Idle:
        recordButton->setText("Start Recording");
        recordButton->setEnabled(true);
        // 只有在已录制一段后才允许处理
        processButton->setEnabled(!m_currentSegmentPath.isEmpty());
        playbackButton->setVisible(false);
        playbackSlider->setVisible(false);
        break;

    case AppState::Recording:
        recordButton->setText("Stop Recording");
        recordButton->setEnabled(true);
        processButton->setEnabled(false); // 录制时不允许处理
        playbackButton->setVisible(false);
        playbackSlider->setVisible(false);
        break;

    case AppState::Processing:
        recordButton->setEnabled(false); // 处理时锁定所有操作
        processButton->setEnabled(false);
        playbackButton->setVisible(false);
        playbackSlider->setVisible(false);
        view_Deblurred->setText("Processing... (Python is running)");
        break;

    case AppState::Playback_Paused:
    case AppState::Playback_Playing:
        recordButton->setEnabled(false); // 回放时不允许录制/处理
        processButton->setEnabled(false);
        playbackButton->setVisible(true);
        playbackSlider->setVisible(true);
        playbackButton->setText(m_currentState == AppState::Playback_Playing ? "Pause" : "Play");
        playbackSlider->setEnabled(m_currentState == AppState::Playback_Paused);
        break;
    }
}

// --- 3. 按钮槽函数 ---

void GUI::onRecordButtonClicked()
{
    if (m_currentState == AppState::Recording) {
        // --- 停止录制 ---
        stopRecording();
        view_RGB->setText("Preview Stopped.");
        view_Deblurred->setText(QString("Segment %1 Recorded.\nReady to process.").arg(m_segmentCounter));
        setUiState(AppState::Idle);
    }
    else if (m_currentState == AppState::Idle) {
        // --- 开始录制 ---
        if (datasetInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "请输入数据集名称。");
            return;
        }
        startRecording();
        view_Deblurred->setText(QString("Recording Segment %1...").arg(m_segmentCounter));
        setUiState(AppState::Recording);
    }
}

void GUI::onProcessButtonClicked()
{
    if (m_currentState == AppState::Idle && !m_currentSegmentPath.isEmpty()) {
        launchProcessing();
    }
}

void GUI::onPlaybackButtonClicked()
{
    if (m_currentState == AppState::Playback_Playing) {
        // --- 暂停 ---
        m_playbackTimer->stop();
        setUiState(AppState::Playback_Paused);
    }
    else if (m_currentState == AppState::Playback_Paused) {
        // --- 播放 ---
        m_playbackTimer->start(33); // 约 30 FPS
        setUiState(AppState::Playback_Playing);
    }
}

void GUI::onSliderMoved(int frame_index)
{
    // 只有在暂停时才能拖动滑块
    if (m_currentState == AppState::Playback_Paused) {
        m_playbackIndex = frame_index;
        showFrame(m_playbackIndex);
    }
}

// --- 4. 录制逻辑 (C++) ---

void GUI::startRecording()
{
    std::string dataset_name = datasetInput->text().toStdString();
    m_segmentCounter++;
    std::string segment_name = "segment_" + std::to_string(m_segmentCounter);

    // (注意 DVS.start 和 RGB.startCapture 接受不同格式的路径)
    std::string segment_path_str = "./" + dataset_name + "/" + segment_name;
    m_currentSegmentPath = QDir::toNativeSeparators(QString::fromStdString(segment_path_str));

    QDir().mkpath(m_currentSegmentPath);

    // 启动硬件录制
    // DVS.start() 需要 "dataset_name/segment_N" 格式
    std::string dvs_name_arg = dataset_name + "/" + segment_name;
    dvs.start(dvs_name_arg);

    // RGB.startCapture() 需要文件夹路径
    rgb.startCapture(m_currentSegmentPath.toStdString());

    // 启动触发器
    uno.start(); //

    // 启动实时预览定时器
    m_livePreviewTimer->start(33); // 约 30 FPS
}

void GUI::stopRecording()
{
    m_livePreviewTimer->stop();
    uno.stop(); //

    // 停止并等待录制线程完成 (HDF5 和 RAW 文件被安全关闭)
    rgb.stopCapture();
    dvs.stopRecord();
}

// QTimer 槽：用于实时预览
void GUI::updateLivePreview()
{
    cv::Mat temp_bgr_frame;
    rgb.getLatestFrame(&temp_bgr_frame); //

    if (temp_bgr_frame.empty()) return;

    // 转换为 Qt 格式 (BGR -> RGB)
    cv::Mat temp_rgb_frame;
    cv::cvtColor(temp_bgr_frame, temp_rgb_frame, cv::COLOR_BGR2RGB);

    QImage qimg(temp_rgb_frame.data,
        temp_rgb_frame.cols,
        temp_rgb_frame.rows,
        (int)temp_rgb_frame.step,
        QImage::Format_RGB888);

    view_RGB->setPixmap(QPixmap::fromImage(qimg.copy())); // .copy() 确保数据安全
}

// --- 5. 处理逻辑 (Python) ---

void GUI::launchProcessing()
{
    setUiState(AppState::Processing);

    // !!! 关键：您必须在此处设置您的 Python 环境 !!!
    // 假设 "python" 在您的系统 PATH 中，并且安装了所有库
    // (例如，您激活了 Anaconda 环境)
    QString python_executable = "python";

    // !!! 关键：您必须设置 Python 脚本的路径 !!!
    QString script_path = "./master_process.py"; // <--- !! 修改此路径 !!

    if (!QFile::exists(script_path)) {
        QMessageBox::critical(this, "Error", "Python 脚本未找到: " + script_path);
        setUiState(AppState::Idle);
        return;
    }

    QStringList args;
    args << script_path << "--path" << m_currentSegmentPath;

    qDebug() << "Starting Python process: " << python_executable << args;

    m_pythonProcess->start(python_executable, args);
}

void GUI::onPythonOutput()
{
    // 打印 Python 的调试输出
    qDebug() << "Python:" << m_pythonProcess->readAllStandardOutput();
}

void GUI::onPythonError()
{
    qWarning() << "Python Error:" << m_pythonProcess->readAllStandardError();
}

void GUI::onPythonFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "Python process finished. Exit code:" << exitCode;

    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        // --- 成功 ---
        view_Deblurred->setText("Processing Complete. Loading data...");
        // 启动回放加载
        setupPlayback(m_currentSegmentPath);
    }
    else {
        // --- 失败 ---
        QMessageBox::critical(this, "Processing Failed", "Python 脚本运行失败。请检查控制台输出。");
        view_Deblurred->setText("Processing FAILED.");
        setUiState(AppState::Idle);
    }
}

// --- 6. 回放逻辑 (C++) ---

void GUI::setupPlayback(const QString& segmentPath)
{
    // 1. 清空旧数据
    m_blurryFrames.clear();
    m_deblurredFrames.clear();

    // 2. 定义 Python 脚本的输出路径 (基于)
    QDir blurry_dir(segmentPath + "/rgb_crop");
    QDir deblurred_dir(segmentPath + "/deblurred");

    // 过滤以 .png 结尾的文件并排序
    QStringList filters = { "*.png" };
    QStringList blurry_files = blurry_dir.entryList(filters, QDir::Files, QDir::Name);
    QStringList deblurred_files = deblurred_dir.entryList(filters, QDir::Files, QDir::Name);

    if (blurry_files.isEmpty() || deblurred_files.isEmpty()) {
        QMessageBox::critical(this, "Playback Error", "未找到 Python 处理后的图像文件 (rgb_crop 或 deblurred 文件夹为空)。");
        setUiState(AppState::Idle);
        return;
    }

    if (blurry_files.size() != deblurred_files.size()) {
        qWarning() << "Warning: Blurry and deblurred frame counts do not match.";
    }

    // 3. 一次性加载所有图像到内存
    int frame_count = qMin(blurry_files.size(), deblurred_files.size());
    for (int i = 0; i < frame_count; ++i) {
        cv::Mat blurry_img = cv::imread(blurry_dir.filePath(blurry_files[i]).toStdString());
        cv::Mat deblurred_img = cv::imread(deblurred_dir.filePath(deblurred_files[i]).toStdString());

        if (blurry_img.empty() || deblurred_img.empty()) {
            qWarning() << "Failed to load frame" << i;
            continue;
        }

        m_blurryFrames.push_back(blurry_img);
        m_deblurredFrames.push_back(deblurred_img);
    }

    if (m_blurryFrames.empty()) {
        QMessageBox::critical(this, "Playback Error", "加载所有图像均失败。");
        setUiState(AppState::Idle);
        return;
    }

    // 4. 设置回放状态
    m_playbackIndex = 0;
    playbackSlider->setRange(0, m_blurryFrames.size() - 1);

    // 5. 显示第一帧并进入暂停状态
    showFrame(0);
    setUiState(AppState::Playback_Paused);
    view_Deblurred->setText(""); // 清空状态文本
}

// QTimer 槽：用于回放循环
void GUI::updatePlayback()
{
    if (m_blurryFrames.empty()) return;

    m_playbackIndex++;
    if (m_playbackIndex >= m_blurryFrames.size()) {
        m_playbackIndex = 0; // 循环播放
    }

    showFrame(m_playbackIndex);
}

// 辅助函数：在两个窗口中同步显示第 N 帧
void GUI::showFrame(int index)
{
    if (index < 0 || index >= m_blurryFrames.size()) return;

    // 1. 获取帧 (它们是 BGR)
    const cv::Mat& blurry = m_blurryFrames[index];
    const cv::Mat& deblurred = m_deblurredFrames[index];

    // 2. 转换 blurry (BGR -> RGB)
    cv::Mat blurry_rgb;
    cv::cvtColor(blurry, blurry_rgb, cv::COLOR_BGR2RGB);
    QImage qimg_blurry(blurry_rgb.data,
        blurry_rgb.cols,
        blurry_rgb.rows,
        (int)blurry_rgb.step,
        QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(qimg_blurry.copy()));

    // 3. 转换 deblurred (BGR -> RGB)
    cv::Mat deblurred_rgb;
    cv::cvtColor(deblurred, deblurred_rgb, cv::COLOR_BGR2RGB);
    QImage qimg_deblurred(deblurred_rgb.data,
        deblurred_rgb.cols,
        deblurred_rgb.rows,
        (int)deblurred_rgb.step,
        QImage::Format_RGB888);
    view_Deblurred->setPixmap(QPixmap::fromImage(qimg_deblurred.copy()));

    // 4. 更新滑块 (避免触发 sliderMoved 信号)
    playbackSlider->blockSignals(true);
    playbackSlider->setValue(index);
    playbackSlider->blockSignals(false);
}

// --- 7. 安全关闭 ---

void GUI::closeEvent(QCloseEvent* event)
{
    // 停止所有正在运行的活动
    if (m_currentState == AppState::Recording) {
        stopRecording();
    }
    if (m_currentState == AppState::Processing) {
        m_pythonProcess->kill(); // 强制终止
        m_pythonProcess->waitForFinished();
    }
    m_livePreviewTimer->stop();
    m_playbackTimer->stop();

    event->accept(); // 接受关闭
}