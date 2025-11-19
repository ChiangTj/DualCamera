#include "../include/Gui.h"
#include <opencv2/imgproc.hpp>
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QDebug>
#include <H5Cpp.h> // 必须包含 HDF5 头文件以读取回放数据

// =========================================================
// 1. 构造函数：初始化 UI 和状态
// =========================================================

GUI::GUI(QWidget* parent)
    : QMainWindow(parent),
    m_currentState(AppState::Idle),
    m_segmentCounter(0),
    m_playbackIndex(0),
    m_pythonProcess(nullptr),
    m_processThread(nullptr)
{
    // 初始化所有 UI 控件
    setupUi();

    // 初始化 Python 进程
    m_pythonProcess = new QProcess(this);

    // 初始化定时器
    m_livePreviewTimer = new QTimer(this);
    m_playbackTimer = new QTimer(this);

    // [新增] 启动时加载 Homography 矩阵 (必须存在)
    if (!loadHomography("./homography.xml")) {
        QMessageBox::warning(this, "Initialization Warning",
            "Failed to load 'homography.xml'. Processing functionality will be disabled.\n"
            "Please run 'generate_homography.py' first.");
        processButton->setEnabled(false);
    }
    else {
        qInfo() << "Homography matrix loaded successfully.";
    }

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

    // 4. Python 进程 (推理阶段)
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

    // 停止 Python
    if (m_pythonProcess->state() == QProcess::Running) {
        m_pythonProcess->kill();
        m_pythonProcess->waitForFinished();
    }

    // 停止 C++ 线程
    if (m_processThread) {
        m_processThread->quit();
        m_processThread->wait();
    }

    m_livePreviewTimer->stop();
    m_playbackTimer->stop();
}

// =========================================================
// 2. UI 辅助函数 (setupUi 和 setUiState)
// =========================================================

// 加载单应性矩阵
bool GUI::loadHomography(const QString& path) {
    if (!QFile::exists(path)) return false;
    try {
        cv::FileStorage fs(path.toStdString(), cv::FileStorage::READ);
        if (!fs.isOpened()) return false;
        fs["H"] >> m_homographyMatrix;
        return !m_homographyMatrix.empty();
    }
    catch (...) { return false; }
}

// 创建所有 UI 控件 (保持原有布局)
void GUI::setupUi()
{
    setWindowTitle("Dual Camera High-Performance System");
    resize(QSize(1600, 700));

    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();
    buttonLayout = new QHBoxLayout();

    view_RGB = new QLabel("Live Preview / Input");
    view_Deblurred = new QLabel("Result / Status");
    view_RGB->setFrameStyle(QFrame::Box);
    view_Deblurred->setFrameStyle(QFrame::Box);
    view_RGB->setMinimumSize(800, 600);
    view_Deblurred->setMinimumSize(800, 600);
    view_RGB->setScaledContents(true);
    view_Deblurred->setScaledContents(true);
    view_RGB->setAlignment(Qt::AlignCenter);
    view_Deblurred->setAlignment(Qt::AlignCenter);

    viewLayout->addWidget(view_RGB);
    viewLayout->addWidget(view_Deblurred);

    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name (e.g., 'Test01')");
    datasetLayout->addWidget(new QLabel("Dataset Name:"));
    datasetLayout->addWidget(datasetInput);

    recordButton = new QPushButton("Start Recording");
    processButton = new QPushButton("Process Last Segment");
    playbackButton = new QPushButton("Play");
    playbackSlider = new QSlider(Qt::Horizontal);

    buttonLayout->addWidget(recordButton);
    buttonLayout->addWidget(processButton);
    buttonLayout->addSpacing(50);
    buttonLayout->addWidget(playbackButton);
    buttonLayout->addWidget(playbackSlider);

    auto mainWidget = new QWidget();
    mainLayout->addLayout(viewLayout);
    mainLayout->addLayout(datasetLayout);
    mainLayout->addLayout(buttonLayout);
    mainWidget->setLayout(mainLayout);
    setCentralWidget(mainWidget);
}

// 状态机控制
void GUI::setUiState(AppState newState)
{
    m_currentState = newState;
    switch (m_currentState) {

    case AppState::Idle:
        recordButton->setText("Start Recording");
        recordButton->setEnabled(true);
        processButton->setEnabled(!m_currentSegmentPath.isEmpty() && !m_homographyMatrix.empty());
        playbackButton->setVisible(false);
        playbackSlider->setVisible(false);
        break;

    case AppState::Recording:
        recordButton->setText("Stop Recording");
        recordButton->setEnabled(true);
        processButton->setEnabled(false);
        playbackButton->setVisible(false);
        playbackSlider->setVisible(false);
        break;

    case AppState::Processing: // C++ Running
        recordButton->setEnabled(false);
        processButton->setEnabled(false);
        playbackButton->setVisible(false);
        playbackSlider->setVisible(false);
        view_Deblurred->setText("System: C++ Preprocessing (Chunked Parallel)...");
        break;

    case AppState::Inference: // Python Running
        recordButton->setEnabled(false);
        processButton->setEnabled(false);
        view_Deblurred->setText("System: AI Inference Running...");
        break;

    case AppState::Playback_Paused:
    case AppState::Playback_Playing:
        recordButton->setEnabled(false);
        processButton->setEnabled(false);
        playbackButton->setVisible(true);
        playbackSlider->setVisible(true);
        playbackButton->setText(m_currentState == AppState::Playback_Playing ? "Pause" : "Play");
        playbackSlider->setEnabled(m_currentState == AppState::Playback_Paused);
        break;
    }
}

// =========================================================
// 3. 按钮槽函数
// =========================================================

void GUI::onRecordButtonClicked()
{
    if (m_currentState == AppState::Recording) {
        // --- 停止录制 ---
        stopRecording();
        view_RGB->setText("Recording Stopped.");
        view_Deblurred->setText(QString("Segment %1 Saved.\nReady to process.").arg(m_segmentCounter));
        setUiState(AppState::Idle);
    }
    else if (m_currentState == AppState::Idle) {
        // --- 开始录制 ---
        if (datasetInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a dataset name.");
            return;
        }
        startRecording();
        view_Deblurred->setText(QString("Recording Segment %1...").arg(m_segmentCounter + 1)); // 显示即将录制的段号
        setUiState(AppState::Recording);
    }
}

// [修改] 点击处理按钮：启动 C++ 线程
void GUI::onProcessButtonClicked()
{
    if (m_currentState != AppState::Idle || m_currentSegmentPath.isEmpty()) return;
    if (m_homographyMatrix.empty()) {
        QMessageBox::critical(this, "Error", "Homography matrix not loaded.");
        return;
    }

    setUiState(AppState::Processing);
    view_Deblurred->setText("Initializing DataProcessor...");

    // 1. 创建 Thread 和 Worker
    m_processThread = new QThread;
    // 创建 DataProcessor (传入路径和矩阵)
    DataProcessor* processor = new DataProcessor(m_currentSegmentPath.toStdString(), m_homographyMatrix);

    processor->moveToThread(m_processThread);

    // 2. 连接信号
    // 线程启动 -> processor::process
    connect(m_processThread, &QThread::started, processor, &DataProcessor::process);

    // 进度更新 (跨线程 UI 更新)
    connect(processor, &DataProcessor::progress, this, &GUI::onProcessingProgress);

    // 完成处理
    connect(processor, &DataProcessor::finished, this, &GUI::onProcessingFinished);

    // 资源清理
    connect(processor, &DataProcessor::finished, m_processThread, &QThread::quit);
    connect(processor, &DataProcessor::finished, processor, &QObject::deleteLater);
    connect(m_processThread, &QThread::finished, m_processThread, &QObject::deleteLater);
    connect(m_processThread, &QThread::finished, [this]() { m_processThread = nullptr; });

    // 3. 启动线程
    m_processThread->start();
}

void GUI::onPlaybackButtonClicked()
{
    if (m_currentState == AppState::Playback_Playing) {
        m_playbackTimer->stop();
        setUiState(AppState::Playback_Paused);
    }
    else if (m_currentState == AppState::Playback_Paused) {
        m_playbackTimer->start(33); // 30 FPS
        setUiState(AppState::Playback_Playing);
    }
}

void GUI::onSliderMoved(int frame_index)
{
    if (m_currentState == AppState::Playback_Paused) {
        m_playbackIndex = frame_index;
        showFrame(m_playbackIndex);
    }
}

// =========================================================
// 4. 录制逻辑 (C++) - 保持不变
// =========================================================

void GUI::startRecording()
{
    std::string dataset_name = datasetInput->text().toStdString();
    m_segmentCounter++;
    std::string segment_name = "segment_" + std::to_string(m_segmentCounter);

    std::string segment_path_str = "./" + dataset_name + "/" + segment_name;
    m_currentSegmentPath = QDir::toNativeSeparators(QString::fromStdString(segment_path_str));

    QDir().mkpath(m_currentSegmentPath);

    // DVS 启动 (需要 "dataset/segment" 格式)
    dvs.start(dataset_name + "/" + segment_name);

    // RGB 启动 (需要全路径)
    rgb.startCapture(m_currentSegmentPath.toStdString());

    // 触发器启动
    uno.start();

    m_livePreviewTimer->start(33);
}

void GUI::stopRecording()
{
    m_livePreviewTimer->stop();
    uno.stop();
    rgb.stopCapture(); // 这里会关闭 HDF5
    dvs.stopRecord();  // 这里会停止 RAW 录制
}

void GUI::updateLivePreview()
{
    cv::Mat temp_bgr_frame;
    rgb.getLatestFrame(&temp_bgr_frame);

    if (temp_bgr_frame.empty()) return;

    cv::Mat temp_rgb_frame;
    cv::cvtColor(temp_bgr_frame, temp_rgb_frame, cv::COLOR_BGR2RGB);

    QImage qimg(temp_rgb_frame.data,
        temp_rgb_frame.cols,
        temp_rgb_frame.rows,
        (int)temp_rgb_frame.step,
        QImage::Format_RGB888);

    view_RGB->setPixmap(QPixmap::fromImage(qimg.copy()));
}

// =========================================================
// 5. 处理逻辑 (C++ DataProcessor Callback)
// =========================================================

// [新增] 进度槽
void GUI::onProcessingProgress(const QString& message)
{
    view_Deblurred->setText(message);
}

// [新增] 完成槽
void GUI::onProcessingFinished(bool success)
{
    if (!success) {
        QMessageBox::critical(this, "Processing Failed", "C++ Data Preprocessing failed.\nCheck console for error details.");
        setUiState(AppState::Idle);
        view_Deblurred->setText("Preprocessing FAILED.");
        return;
    }

    // C++ 成功 -> 启动 Python 推理
    launchPythonInference();
}

// =========================================================
// 6. 推理逻辑 (Python)
// =========================================================
void GUI::launchPythonInference()
{
    setUiState(AppState::Inference);

    // 1. 设置 Python 环境和脚本路径
    // 建议：如果在发布版本中，尽量不要硬编码 python，而是读取环境变量或配置文件
    QString python_executable = "python";

    // 指向新创建的 wrapper 脚本
    QString script_path = "./run_inference.py";
    QString config_path = "./real.yml";

    // 检查脚本是否存在
    if (!QFile::exists(script_path)) {
        QMessageBox::critical(this, "Error", "Python script 'run_inference.py' not found.");
        setUiState(AppState::Idle);
        return;
    }

    // 2. 构建参数列表
    QStringList args;

    // 对应 run_inference.py 中的 parser.add_argument
    args << script_path;
    args << "-opt" << config_path;

    // 关键：传递动态的数据路径
    // 确保 m_currentSegmentPath 是标准路径格式 (例如把 \ 替换为 /，避免 Python 转义问题)
    QString cleanPath = m_currentSegmentPath;
    cleanPath.replace("\\", "/");

    args << "--dataroot" << cleanPath;

    // 调试输出
    qDebug() << "Launching Python:" << python_executable << args.join(" ");

    // 3. 启动进程
    // 建议设置工作目录，确保 Python 能找到 basicsr 模块
    m_pythonProcess->setWorkingDirectory(QCoreApplication::applicationDirPath());
    // 或者显式指定为 script_path 所在的目录

    m_pythonProcess->start(python_executable, args);
}

void GUI::onPythonOutput() {
    qDebug() << "[Python]" << m_pythonProcess->readAllStandardOutput();
}

void GUI::onPythonError() {
    qWarning() << "[Python Error]" << m_pythonProcess->readAllStandardError();
}

void GUI::onPythonFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        view_Deblurred->setText("Inference Complete. Loading Results...");
        setupPlayback(m_currentSegmentPath);
    }
    else {
        QMessageBox::critical(this, "Inference Failed", "Python script failed.");
        setUiState(AppState::Idle);
        view_Deblurred->setText("Inference FAILED.");
    }
}

// =========================================================
// 7. 回放逻辑 (HDF5 Input + PNG Output)
// =========================================================

void GUI::setupPlayback(const QString& segmentPath)
{
    m_blurryFrames.clear();
    m_deblurredFrames.clear();

    // --- 1. 读取 Input Frames (从 processed_data.h5) ---
    try {
        std::string h5_path = segmentPath.toStdString() + "/processed_data.h5";

        // 使用 HDF5 C++ API 打开
        H5::H5File f(h5_path, H5F_ACC_RDONLY);
        H5::DataSet ds = f.openDataSet("rgb_aligned");

        // 获取维度 [N, H, W, C]
        hsize_t dims[4];
        ds.getSpace().getSimpleExtentDims(dims, NULL);
        int frame_count = (int)dims[0];
        int h = (int)dims[1]; // 720
        int w = (int)dims[2]; // 1000
        int c = (int)dims[3]; // 3

        // 一次性读取到内存 Buffer (比逐帧读快得多)
        // 100帧 ~ 200MB, 内存完全够用
        size_t total_bytes = (size_t)frame_count * h * w * c;
        std::vector<unsigned char> buffer(total_bytes);
        ds.read(buffer.data(), H5::PredType::NATIVE_UINT8);

        // 转换为 OpenCV Mat
        for (int i = 0; i < frame_count; ++i) {
            unsigned char* ptr = buffer.data() + (size_t)i * h * w * c;
            cv::Mat img(h, w, CV_8UC3, ptr);
            m_blurryFrames.push_back(img.clone()); // 深拷贝
        }

        qDebug() << "Loaded" << frame_count << "input frames from HDF5.";

    }
    catch (H5::Exception& e) {
        QMessageBox::warning(this, "Playback Error",
            QString("Failed to load input HDF5: %1").arg(e.getCDetailMsg()));
        setUiState(AppState::Idle);
        return;
    }
    catch (...) {
        QMessageBox::warning(this, "Playback Error", "Unknown error loading HDF5.");
        setUiState(AppState::Idle);
        return;
    }

    // --- 2. 读取 Output Frames (从 PNG 文件夹) ---
    QDir deblurred_dir(segmentPath + "/deblurred/final_output");
    if (!deblurred_dir.exists()) {
        deblurred_dir.setPath(segmentPath + "/deblurred"); // 尝试上一级
    }

    QStringList filters = { "*.png" };
    QStringList files = deblurred_dir.entryList(filters, QDir::Files, QDir::Name); // 按名称排序

    for (const auto& filename : files) {
        std::string filepath = deblurred_dir.filePath(filename).toStdString();
        cv::Mat img = cv::imread(filepath);
        if (!img.empty()) {
            m_deblurredFrames.push_back(img);
        }
    }

    if (m_deblurredFrames.empty()) {
        // 仅警告，不阻止播放原图
        qWarning() << "No deblurred result images found.";
        view_Deblurred->setText("No Result Images Found.");
    }

    // --- 3. 启动回放 ---
    m_playbackIndex = 0;
    int max_frames = m_blurryFrames.size();
    // 如果有结果图，取较小值防止越界；如果没结果图，就只播原图
    if (!m_deblurredFrames.empty()) {
        max_frames = (std::min)(max_frames, (int)m_deblurredFrames.size());
    }

    if (max_frames == 0) {
        setUiState(AppState::Idle);
        return;
    }

    playbackSlider->setRange(0, max_frames - 1);
    setUiState(AppState::Playback_Paused);
    showFrame(0);
}

void GUI::updatePlayback()
{
    if (m_blurryFrames.empty()) return;

    m_playbackIndex++;
    // 循环播放
    int max_idx = playbackSlider->maximum();
    if (m_playbackIndex > max_idx) {
        m_playbackIndex = 0;
    }
    showFrame(m_playbackIndex);
}

void GUI::showFrame(int index)
{
    if (index < 0 || index >= m_blurryFrames.size()) return;

    // 显示 Input (Blurry)
    const cv::Mat& blurry = m_blurryFrames[index];
    cv::Mat blurry_rgb;
    cv::cvtColor(blurry, blurry_rgb, cv::COLOR_BGR2RGB);
    QImage qimg_blurry(blurry_rgb.data, blurry_rgb.cols, blurry_rgb.rows, (int)blurry_rgb.step, QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(qimg_blurry.copy()));

    // 显示 Result (Deblurred) - 只有存在时才显示
    if (index < m_deblurredFrames.size()) {
        const cv::Mat& deblurred = m_deblurredFrames[index];
        cv::Mat deblurred_rgb;
        cv::cvtColor(deblurred, deblurred_rgb, cv::COLOR_BGR2RGB);
        QImage qimg_deblurred(deblurred_rgb.data, deblurred_rgb.cols, deblurred_rgb.rows, (int)deblurred_rgb.step, QImage::Format_RGB888);
        view_Deblurred->setPixmap(QPixmap::fromImage(qimg_deblurred.copy()));
    }
    else if (m_deblurredFrames.empty()) {
        // 保持之前的 "No Result" 文本，不清除
    }

    // 更新滑块
    playbackSlider->blockSignals(true);
    playbackSlider->setValue(index);
    playbackSlider->blockSignals(false);
}

void GUI::closeEvent(QCloseEvent* event)
{
    // 析构函数会处理清理
    event->accept();
}