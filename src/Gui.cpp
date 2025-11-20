#include "../include/Gui.h"
#include <opencv2/imgproc.hpp>
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QDebug>
#include <QThread> // 确保包含
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
    // 设置进程环境，合并当前系统环境变量
    m_pythonProcess->setProcessEnvironment(QProcessEnvironment::systemEnvironment());

    // 初始化定时器
    m_livePreviewTimer = new QTimer(this);
    m_playbackTimer = new QTimer(this);

    // 启动时加载 Homography 矩阵 (必须存在)
    // 如果是调试模式，允许不存在，但禁用处理按钮
    if (!loadHomography("./homography.xml")) {
        view_Deblurred->setText("Warning: Homography not found.\nProcessing disabled.");
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
    // 1. 停止录制 (最优先)
    if (m_currentState == AppState::Recording) {
        stopRecording();
        // 给一点时间让 HDF5 写入完成，防止文件损坏
        QThread::msleep(500);
    }

    // 2. 停止 Python
    if (m_pythonProcess && m_pythonProcess->state() == QProcess::Running) {
        m_pythonProcess->kill();
        m_pythonProcess->waitForFinished();
    }

    // 3. 停止 C++ 线程
    if (m_processThread) {
        m_processThread->quit();
        m_processThread->wait();
    }

    if (m_livePreviewTimer) m_livePreviewTimer->stop();
    if (m_playbackTimer) m_playbackTimer->stop();
}

// =========================================================
// 2. UI 辅助函数
// =========================================================

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

void GUI::setupUi()
{
    setWindowTitle("Dual Camera High-Performance System (Release v1.0)");
    resize(QSize(1600, 800)); // 稍微调大一点

    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();
    buttonLayout = new QHBoxLayout();

    view_RGB = new QLabel("Live Preview / Input");
    view_Deblurred = new QLabel("Result / Status");

    // 设置样式
    QString labelStyle = "QLabel { background-color : #202020; color : white; border: 2px solid #505050; }";
    view_RGB->setStyleSheet(labelStyle);
    view_Deblurred->setStyleSheet(labelStyle);

    view_RGB->setMinimumSize(800, 600);
    view_Deblurred->setMinimumSize(800, 600);
    view_RGB->setScaledContents(true);
    view_Deblurred->setScaledContents(true);
    view_RGB->setAlignment(Qt::AlignCenter);
    view_Deblurred->setAlignment(Qt::AlignCenter);

    viewLayout->addWidget(view_RGB);
    viewLayout->addWidget(view_Deblurred);

    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name (e.g., 'Demo01')");
    datasetLayout->addWidget(new QLabel("Dataset Name:"));
    datasetLayout->addWidget(datasetInput);

    recordButton = new QPushButton("Start Recording");
    processButton = new QPushButton("Process Last Segment");
    playbackButton = new QPushButton("Play");
    playbackSlider = new QSlider(Qt::Horizontal);

    // 设置按钮高度，方便触摸或点击
    recordButton->setMinimumHeight(40);
    processButton->setMinimumHeight(40);
    playbackButton->setMinimumHeight(40);

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

void GUI::setUiState(AppState newState)
{
    m_currentState = newState;

    // 默认禁用回放
    playbackButton->setVisible(false);
    playbackSlider->setVisible(false);

    switch (m_currentState) {
    case AppState::Idle:
        recordButton->setText("Start Recording");
        recordButton->setEnabled(true);
        // 只有在有录制记录且加载了矩阵时才允许处理
        processButton->setEnabled(!m_currentSegmentPath.isEmpty() && !m_homographyMatrix.empty());

        // 如果之前已经加载了回放数据，允许播放
        if (!m_blurryFrames.empty()) {
            playbackButton->setVisible(true);
            playbackSlider->setVisible(true);
            playbackButton->setText("Play");
        }
        break;

    case AppState::Recording:
        recordButton->setText("Stop Recording");
        recordButton->setStyleSheet("background-color: red; color: white;");
        recordButton->setEnabled(true);
        processButton->setEnabled(false);
        datasetInput->setEnabled(false); // 录制时禁止修改名称
        break;

    case AppState::Processing:
        recordButton->setEnabled(false);
        processButton->setEnabled(false);
        datasetInput->setEnabled(false);
        view_Deblurred->setText("System: C++ Processing (Remap & Voxel)...\nPlease Wait.");
        break;

    case AppState::Inference:
        recordButton->setEnabled(false);
        processButton->setEnabled(false);
        view_Deblurred->setText("System: Python AI Inference Running...\nThis may take a while.");
        break;

    case AppState::Playback_Paused:
    case AppState::Playback_Playing:
        recordButton->setText("Start Recording");
        recordButton->setStyleSheet(""); // 恢复默认样式
        recordButton->setEnabled(true);
        processButton->setEnabled(true);
        datasetInput->setEnabled(true);

        playbackButton->setVisible(true);
        playbackSlider->setVisible(true);
        playbackButton->setText(m_currentState == AppState::Playback_Playing ? "Pause" : "Play");
        playbackSlider->setEnabled(true); // 即使播放中也允许拖动
        break;
    }

    // 如果不是录制状态，恢复按钮样式
    if (m_currentState != AppState::Recording) {
        recordButton->setStyleSheet("");
        datasetInput->setEnabled(true);
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
        view_Deblurred->setText(QString("Segment Saved to:\n%1\n\nClick 'Process' to start.").arg(m_currentSegmentPath));
        setUiState(AppState::Idle);
    }
    else if (m_currentState == AppState::Idle || m_currentState == AppState::Playback_Paused) {
        // --- 开始录制 ---
        if (datasetInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a dataset name first.");
            return;
        }
        startRecording();
        setUiState(AppState::Recording);
        view_Deblurred->setText(QString("Recording Segment %1...").arg(m_segmentCounter));
    }
}

void GUI::onProcessButtonClicked()
{
    if (m_currentSegmentPath.isEmpty()) return;

    // 检查文件是否存在
    QString rawPath = m_currentSegmentPath + QDir::separator() + QDir(m_currentSegmentPath).dirName() + ".raw";
    QString h5Path = m_currentSegmentPath + "/rgb_data.h5";

    if (!QFile::exists(rawPath) || !QFile::exists(h5Path)) {
        QMessageBox::critical(this, "Error", "Data files not found in:\n" + m_currentSegmentPath);
        return;
    }

    if (m_homographyMatrix.empty()) {
        QMessageBox::critical(this, "Error", "Homography matrix not loaded.");
        return;
    }

    setUiState(AppState::Processing);

    // 创建并启动线程
    if (m_processThread) {
        m_processThread->quit();
        m_processThread->wait();
        delete m_processThread;
        m_processThread = nullptr;
    }

    m_processThread = new QThread;
    DataProcessor* processor = new DataProcessor(m_currentSegmentPath.toStdString(), m_homographyMatrix);
    processor->moveToThread(m_processThread);

    connect(m_processThread, &QThread::started, processor, &DataProcessor::process);
    connect(processor, &DataProcessor::progress, this, &GUI::onProcessingProgress);
    connect(processor, &DataProcessor::finished, this, &GUI::onProcessingFinished);

    // 自动清理
    connect(processor, &DataProcessor::finished, m_processThread, &QThread::quit);
    connect(processor, &DataProcessor::finished, processor, &QObject::deleteLater);
    connect(m_processThread, &QThread::finished, m_processThread, &QObject::deleteLater);
    connect(m_processThread, &QThread::finished, [this]() { m_processThread = nullptr; });

    m_processThread->start();
}

void GUI::onPlaybackButtonClicked()
{
    if (m_currentState == AppState::Playback_Playing) {
        m_playbackTimer->stop();
        setUiState(AppState::Playback_Paused);
    }
    else {
        // 33ms = ~30fps
        m_playbackTimer->start(33);
        setUiState(AppState::Playback_Playing);
    }
}

void GUI::onSliderMoved(int frame_index)
{
    m_playbackIndex = frame_index;
    // 暂停播放以便用户拖动查看
    if (m_currentState == AppState::Playback_Playing) {
        m_playbackTimer->stop();
        setUiState(AppState::Playback_Paused);
    }
    showFrame(m_playbackIndex);
}

// =========================================================
// 4. 录制逻辑
// =========================================================

void GUI::startRecording()
{
    std::string dataset_name = datasetInput->text().toStdString();
    m_segmentCounter++;
    std::string segment_name = "segment_" + std::to_string(m_segmentCounter);

    std::string segment_path_str = "./" + dataset_name + "/" + segment_name;
    m_currentSegmentPath = QDir::toNativeSeparators(QString::fromStdString(segment_path_str));

    QDir().mkpath(m_currentSegmentPath);

    // 注意：DVS SDK 可能需要特定的路径格式
    dvs.start(dataset_name + "/" + segment_name);
    rgb.startCapture(m_currentSegmentPath.toStdString());
    uno.start(); // 触发器

    m_livePreviewTimer->start(33);
}

void GUI::stopRecording()
{
    m_livePreviewTimer->stop();
    uno.stop();
    rgb.stopCapture(); // 重要：这会关闭 HDF5 文件头
    dvs.stopRecord();
}

void GUI::updateLivePreview()
{
    cv::Mat temp_bgr_frame;
    rgb.getLatestFrame(&temp_bgr_frame);

    if (temp_bgr_frame.empty()) return;

    // 显示 DVS 叠加层 (可选) - 这里只显示 RGB
    cv::Mat temp_rgb_frame;
    cv::cvtColor(temp_bgr_frame, temp_rgb_frame, cv::COLOR_BGR2RGB);

    // 缩放显示以适应 Label
    QImage qimg(temp_rgb_frame.data, temp_rgb_frame.cols, temp_rgb_frame.rows, (int)temp_rgb_frame.step, QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(qimg));
}

// =========================================================
// 5. C++ 处理回调
// =========================================================

void GUI::onProcessingProgress(const QString& message)
{
    view_Deblurred->setText(message);
}

void GUI::onProcessingFinished(bool success)
{
    if (!success) {
        QMessageBox::critical(this, "Processing Failed", "DataProcessor encountered an error.\nCheck the console output.");
        setUiState(AppState::Idle);
        view_Deblurred->setText("Preprocessing Failed.");
        return;
    }

    // C++ 成功后，自动启动 Python
    launchPythonInference();
}

// =========================================================
// 6. Python 推理逻辑
// =========================================================

void GUI::launchPythonInference()
{
    setUiState(AppState::Inference);

    // 配置 Python 路径 (根据实际情况修改)
    // 建议：使用绝对路径指向 Conda 环境的 python.exe
    QString python_executable = "python";
    // 示例: QString python_executable = "C:/Anaconda3/envs/pytorch/python.exe";

    QString script_path = "./run_inference.py";
    QString config_path = "./real.yml";

    if (!QFile::exists(script_path)) {
        QMessageBox::critical(this, "Error", "Inference script not found:\n" + script_path);
        setUiState(AppState::Idle);
        return;
    }

    QStringList args;
    args << script_path;
    args << "-opt" << config_path;

    // 统一路径分隔符，防止 Windows 下反斜杠被 Python 解析为转义符
    QString cleanPath = m_currentSegmentPath;
    cleanPath.replace("\\", "/");
    args << "--dataroot" << cleanPath;

    qDebug() << "Executing:" << python_executable << args.join(" ");

    m_pythonProcess->setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_pythonProcess->start(python_executable, args);
}

void GUI::onPythonOutput() {
    QString out = m_pythonProcess->readAllStandardOutput();
    // 将 Python 的 print 输出显示在状态栏或 Debug 窗口
    if (out.contains("Processing")) view_Deblurred->setText("AI: " + out.trimmed());
    qDebug() << "[Python]" << out.trimmed();
}

void GUI::onPythonError() {
    qWarning() << "[Python ERR]" << m_pythonProcess->readAllStandardError().trimmed();
}

void GUI::onPythonFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        view_Deblurred->setText("Inference Done. Loading...");
        setupPlayback(m_currentSegmentPath);
    }
    else {
        QMessageBox::critical(this, "Error", "Python Inference Failed.\nExit Code: " + QString::number(exitCode));
        setUiState(AppState::Idle);
        view_Deblurred->setText("AI Inference Failed.");
    }
}

// =========================================================
// 7. 回放逻辑
// =========================================================

void GUI::setupPlayback(const QString& segmentPath)
{
    // 清空旧数据
    m_blurryFrames.clear();
    m_deblurredFrames.clear();

    // 1. 加载 C++ 处理后的对齐输入 (processed_data.h5)
    try {
        std::string h5_path = segmentPath.toStdString() + "/processed_data.h5";

        H5::H5File f(h5_path, H5F_ACC_RDONLY);
        H5::DataSet ds = f.openDataSet("rgb_aligned"); // 注意：这是 Remap 后的 1000x720 图像

        hsize_t dims[4];
        ds.getSpace().getSimpleExtentDims(dims, NULL);
        int N = dims[0];
        int H = dims[1];
        int W = dims[2];
        int C = dims[3];

        // 内存预估检查 (防止炸内存)
        size_t total_size = (size_t)N * H * W * C;
        if (total_size > 4ULL * 1024 * 1024 * 1024) { // > 4GB
            QMessageBox::warning(this, "Warning", "Dataset is too large to load fully into RAM.\nPlayback may fail.");
        }

        // 一次性读取
        std::vector<uint8_t> buffer(total_size);
        ds.read(buffer.data(), H5::PredType::NATIVE_UINT8);

        m_blurryFrames.reserve(N);
        for (int i = 0; i < N; ++i) {
            uint8_t* frame_ptr = buffer.data() + (size_t)i * H * W * C;
            // 深拷贝到 cv::Mat
            cv::Mat img(H, W, CV_8UC3, frame_ptr);
            m_blurryFrames.push_back(img.clone());
        }

        qDebug() << "Loaded" << N << "frames from HDF5.";

    }
    catch (...) {
        QMessageBox::warning(this, "Error", "Failed to load processed_data.h5");
        setUiState(AppState::Idle);
        return;
    }

    // 2. 加载 Python 输出结果 (PNG 序列)
    // 假设路径结构: segment/deblurred/final_output/*.png
    QDir resultDir(segmentPath + "/deblurred/final_output");
    if (!resultDir.exists()) {
        // 尝试备用路径
        resultDir.setPath(segmentPath + "/deblurred");
    }

    QStringList filters; filters << "*.png" << "*.jpg";
    QStringList files = resultDir.entryList(filters, QDir::Files, QDir::Name);

    m_deblurredFrames.reserve(files.size());
    for (const auto& file : files) {
        cv::Mat img = cv::imread(resultDir.filePath(file).toStdString());
        if (!img.empty()) m_deblurredFrames.push_back(img);
    }

    if (m_blurryFrames.empty()) {
        view_Deblurred->setText("Error: No frames loaded.");
        setUiState(AppState::Idle);
        return;
    }

    // 3. 准备播放
    m_playbackIndex = 0;
    int max_frames = m_blurryFrames.size();
    playbackSlider->setRange(0, max_frames - 1);

    setUiState(AppState::Playback_Paused);
    showFrame(0);
}

void GUI::updatePlayback()
{
    if (m_blurryFrames.empty()) return;

    m_playbackIndex++;
    if (m_playbackIndex >= m_blurryFrames.size()) {
        m_playbackIndex = 0; // 循环
    }
    showFrame(m_playbackIndex);
}

void GUI::showFrame(int index)
{
    if (index < 0 || index >= m_blurryFrames.size()) return;

    // Input
    cv::Mat in_rgb;
    cv::cvtColor(m_blurryFrames[index], in_rgb, cv::COLOR_BGR2RGB);
    QImage q_in(in_rgb.data, in_rgb.cols, in_rgb.rows, in_rgb.step, QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(q_in));

    // Output
    if (index < m_deblurredFrames.size()) {
        cv::Mat out_rgb;
        cv::cvtColor(m_deblurredFrames[index], out_rgb, cv::COLOR_BGR2RGB);
        QImage q_out(out_rgb.data, out_rgb.cols, out_rgb.rows, out_rgb.step, QImage::Format_RGB888);
        view_Deblurred->setPixmap(QPixmap::fromImage(q_out));
    }
    else {
        // 如果结果帧数少于输入帧数 (比如 Python 处理失败了一部分)
        view_Deblurred->setText("No Result Frame");
    }

    // 更新滑块而不触发信号
    playbackSlider->blockSignals(true);
    playbackSlider->setValue(index);
    playbackSlider->blockSignals(false);
}

// 窗口关闭保护
void GUI::closeEvent(QCloseEvent* event)
{
    if (m_currentState == AppState::Recording) {
        // 强制停止并等待，防止 HDF5 损坏
        stopRecording();
        QThread::msleep(500);
    }
    event->accept();
}