#include "../include/Gui.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QMetaObject>

#include <opencv2/imgproc.hpp>

#include <stdexcept>

GUI::GUI(QWidget* parent)
    : QMainWindow(parent),
    m_currentState(AppState::Idle),
    m_segmentCounter(0),
    m_playbackIndex(0)
{
    setupUi();
    qRegisterMetaType<cv::Mat>("cv::Mat");

    m_livePreviewTimer = new QTimer(this);
    m_playbackTimer = new QTimer(this);

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString xmlPath = QDir::cleanPath(exeDir + "/homography.xml");

    qInfo() << "Attempting to load Homography from:" << xmlPath;
    if (!loadHomography(xmlPath)) {
        view_Deblurred->setText("Error: homography.xml not found at:\n" + xmlPath + "\nProcessing disabled.");
        processButton->setEnabled(false);
    }
    else {
        qInfo() << "Homography matrix loaded successfully.";
    }

    m_trtEngine = std::make_shared<TrtInference>();
    const QString enginePath = QDir::cleanPath(exeDir + "/efnet.engine");
    m_trtReady = m_trtEngine->init(enginePath.toStdString());
    if (!m_trtReady) {
        QMessageBox::critical(this, "Engine Error", "Failed to load TensorRT engine at:\n" + enginePath + "\nInference will not work.");
    }
    else {
        qInfo() << "TensorRT engine loaded successfully from:" << enginePath;
    }

    m_inferenceQueue = std::make_shared<LatestFrameQueue<PreprocessPacket>>(4);
    m_inferenceWorker = new InferenceWorker(m_trtEngine, m_inferenceQueue);
    m_inferenceThread = new QThread(this);
    m_inferenceWorker->moveToThread(m_inferenceThread);

    connect(m_inferenceThread, &QThread::started, m_inferenceWorker, &InferenceWorker::process);
    connect(m_inferenceWorker, &InferenceWorker::resultReady, this, &GUI::onDeblurredImageReady);
    connect(m_inferenceWorker, &InferenceWorker::streamFinished, this, &GUI::onInferenceStreamFinished);
    connect(m_inferenceWorker, &InferenceWorker::inferenceError, this, [](const QString& msg) {
        qWarning() << "Inference Error:" << msg;
        });

    m_inferenceThread->start();

    connect(recordButton, &QPushButton::clicked, this, &GUI::onRecordButtonClicked);
    connect(processButton, &QPushButton::clicked, this, &GUI::onProcessButtonClicked);
    connect(playbackButton, &QPushButton::clicked, this, &GUI::onPlaybackButtonClicked);
    connect(playbackSlider, &QSlider::sliderMoved, this, &GUI::onSliderMoved);
    connect(m_livePreviewTimer, &QTimer::timeout, this, &GUI::updateLivePreview);
    connect(m_playbackTimer, &QTimer::timeout, this, &GUI::updatePlayback);

    setUiState(AppState::Idle);
}

GUI::~GUI()
{
    if (m_currentState == AppState::Recording) {
        stopRecording();
        QThread::msleep(500);
    }
    else {
        stopRealtimeProcessing();
    }

    if (m_inferenceWorker) {
        m_inferenceWorker->stop();
    }
    if (m_inferenceThread) {
        m_inferenceThread->quit();
        m_inferenceThread->wait();
    }

    if (m_processThread) {
        m_processThread->quit();
        m_processThread->wait();
    }

    if (m_livePreviewTimer) m_livePreviewTimer->stop();
    if (m_playbackTimer) m_playbackTimer->stop();
}

bool GUI::loadHomography(const QString& path)
{
    if (!QFile::exists(path)) return false;

    try {
        cv::FileStorage fs(path.toStdString(), cv::FileStorage::READ);
        if (!fs.isOpened()) return false;
        fs["H"] >> m_homographyMatrix;
        return !m_homographyMatrix.empty();
    }
    catch (...) {
        return false;
    }
}

void GUI::setupUi()
{
    setWindowTitle("Dual Camera Real-time Deblur System");
    resize(QSize(1600, 800));

    mainLayout = new QVBoxLayout();
    viewLayout = new QHBoxLayout();
    datasetLayout = new QHBoxLayout();
    buttonLayout = new QHBoxLayout();

    view_RGB = new QLabel("Live Preview / Blurry Input");
    view_Deblurred = new QLabel("Deblurred Output");

    const QString labelStyle = "QLabel { background-color : #202020; color : white; border: 2px solid #505050; }";
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
    processButton = new QPushButton("Process & Infer");
    playbackButton = new QPushButton("Play");
    playbackSlider = new QSlider(Qt::Horizontal);

    recordButton->setMinimumHeight(40);
    processButton->setMinimumHeight(40);
    playbackButton->setMinimumHeight(40);

    buttonLayout->addWidget(recordButton);
    buttonLayout->addWidget(processButton);
    buttonLayout->addSpacing(50);
    buttonLayout->addWidget(playbackButton);
    buttonLayout->addWidget(playbackSlider);

    auto* mainWidget = new QWidget();
    mainLayout->addLayout(viewLayout);
    mainLayout->addLayout(datasetLayout);
    mainLayout->addLayout(buttonLayout);
    mainWidget->setLayout(mainLayout);
    setCentralWidget(mainWidget);
}

void GUI::setUiState(AppState newState)
{
    m_currentState = newState;

    playbackButton->setVisible(false);
    playbackSlider->setVisible(false);

    switch (m_currentState) {
    case AppState::Idle:
        recordButton->setText("Start Recording");
        recordButton->setEnabled(true);
        processButton->setEnabled(!m_currentSegmentPath.isEmpty() && !m_homographyMatrix.empty());

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
        datasetInput->setEnabled(false);
        break;

    case AppState::Processing:
        recordButton->setEnabled(false);
        processButton->setEnabled(false);
        datasetInput->setEnabled(false);
        view_Deblurred->setText("System: Remapping & TensorRT Inference Running...\nPlease Wait.");
        break;

    case AppState::Playback_Paused:
    case AppState::Playback_Playing:
        recordButton->setText("Start Recording");
        recordButton->setStyleSheet("");
        recordButton->setEnabled(true);
        datasetInput->setEnabled(true);
        processButton->setEnabled(!m_currentSegmentPath.isEmpty() && !m_homographyMatrix.empty());

        playbackButton->setVisible(true);
        playbackSlider->setVisible(true);
        playbackButton->setText(m_currentState == AppState::Playback_Playing ? "Pause" : "Play");
        playbackSlider->setEnabled(true);
        break;
    }

    if (m_currentState != AppState::Recording) {
        recordButton->setStyleSheet("");
        datasetInput->setEnabled(true);
    }
}

void GUI::onRecordButtonClicked()
{
    if (m_currentState == AppState::Recording) {
        stopRecording();
        view_RGB->setText("Recording Stopped.");
        view_Deblurred->setText(QString("Segment Saved to:\n%1\n\nClick 'Process & Infer' to start.").arg(m_currentSegmentPath));
        setUiState(AppState::Idle);
    }
    else if (m_currentState == AppState::Idle || m_currentState == AppState::Playback_Paused) {
        if (datasetInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a dataset name first.");
            return;
        }
        startRecording();
    }
}

void GUI::onProcessButtonClicked()
{
    if (m_currentSegmentPath.isEmpty() || m_homographyMatrix.empty()) return;

    setUiState(AppState::Processing);

    m_blurryFrames.clear();
    m_deblurredFrames.clear();
    m_playbackIndex = 0;
    m_processingCompleted = false;
    m_inferenceCompleted = false;
    m_expectedFrameCount = 0;
    m_inferenceQueue->reset();

    if (m_processThread) {
        m_processThread->quit();
        m_processThread->wait();
        delete m_processThread;
        m_processThread = nullptr;
    }

    m_processThread = new QThread;
    auto* processor = new DataProcessor(m_currentSegmentPath.toStdString(), m_homographyMatrix);

    processor->setInferenceQueue(m_inferenceQueue);
    processor->moveToThread(m_processThread);

    connect(m_processThread, &QThread::started, processor, &DataProcessor::process);
    connect(processor, &DataProcessor::progress, this, &GUI::onProcessingProgress);
    connect(processor, &DataProcessor::blurryFrameReady, this, &GUI::onBlurryFrameReady);
    connect(processor, &DataProcessor::finished, this, &GUI::onProcessingFinished);
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
        m_playbackTimer->start(33);
        setUiState(AppState::Playback_Playing);
    }
}

void GUI::onSliderMoved(int frame_index)
{
    m_playbackIndex = frame_index;
    if (m_currentState == AppState::Playback_Playing) {
        m_playbackTimer->stop();
        setUiState(AppState::Playback_Paused);
    }
    showFrame(m_playbackIndex);
}

void GUI::setupRealtimeProcessing()
{
    stopRealtimeProcessing();

    if (!m_trtReady || m_homographyMatrix.empty()) {
        return;
    }

    m_inferenceQueue->reset();
    {
        std::lock_guard<std::mutex> lock(m_realtimePairMutex);
        m_pendingRealtimeRgbFrames.clear();
    }

    m_realtimeQueue = std::make_shared<LatestFrameQueue<RealtimeFrameInput>>(2);
    m_realtimeWorker = new RealtimeProcessorWorker(m_homographyMatrix, m_realtimeQueue, m_inferenceQueue);
    m_realtimeThread = new QThread(this);
    m_realtimeWorker->moveToThread(m_realtimeThread);

    connect(m_realtimeThread, &QThread::started, m_realtimeWorker, &RealtimeProcessorWorker::process);
    connect(m_realtimeWorker, &RealtimeProcessorWorker::preprocessError, this, [](const QString& msg) {
        qWarning() << "Realtime preprocess error:" << msg;
        });
    connect(m_realtimeThread, &QThread::finished, m_realtimeWorker, &QObject::deleteLater);

    rgb.setFrameReadyCallback([this](cv::Mat frame, unsigned int frameNumber) {
        QMetaObject::invokeMethod(this, [this, frame = std::move(frame), frameNumber]() mutable {
            handleRealtimeRgbFrame(std::move(frame), frameNumber);
            }, Qt::QueuedConnection);
        });

    m_realtimeThread->start();
}

void GUI::stopRealtimeProcessing()
{
    rgb.setFrameReadyCallback({});

    {
        std::lock_guard<std::mutex> lock(m_realtimePairMutex);
        m_pendingRealtimeRgbFrames.clear();
    }

    if (m_realtimeWorker) {
        m_realtimeWorker->stop();
    }
    if (m_realtimeQueue) {
        m_realtimeQueue->stop();
    }
    if (m_realtimeThread) {
        m_realtimeThread->quit();
        m_realtimeThread->wait();
        delete m_realtimeThread;
        m_realtimeThread = nullptr;
    }

    m_realtimeWorker = nullptr;
    m_realtimeQueue.reset();
}

void GUI::handleRealtimeRgbFrame(cv::Mat frame, unsigned int frameNumber)
{
    if (m_currentState != AppState::Recording || !m_realtimeQueue || frame.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_realtimePairMutex);

    if (m_pendingRealtimeRgbFrames.size() >= 4) {
        m_pendingRealtimeRgbFrames.pop_front();
        qWarning() << "Dropping oldest pending RGB frame to keep realtime latency bounded.";
    }
    m_pendingRealtimeRgbFrames.emplace_back(std::move(frame), frameNumber);

    DvsEventWindow window;
    while (!m_pendingRealtimeRgbFrames.empty() && dvs.tryPopNextRealtimeWindow(window)) {
        RealtimeFrameInput input;
        input.frameNumber = m_pendingRealtimeRgbFrames.front().second;
        input.rgbFrame = std::move(m_pendingRealtimeRgbFrames.front().first);
        input.eventWindow = std::move(window);
        m_pendingRealtimeRgbFrames.pop_front();

        m_realtimeQueue->pushReplacingOldest(std::move(input));
    }
}

void GUI::startRecording()
{
    const std::string dataset_name = datasetInput->text().toStdString();
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    const QString relative_path = QString("data/%1").arg(QString::fromStdString(dataset_name));
    const QString absolute_path = dir.filePath(relative_path);
    m_currentSegmentPath = QDir::toNativeSeparators(absolute_path);

    if (!QDir().mkpath(m_currentSegmentPath)) {
        QMessageBox::critical(this, "Error", "Failed to create directory:\n" + m_currentSegmentPath);
        return;
    }

    const std::string path_std = m_currentSegmentPath.toStdString();
    try {
        setupRealtimeProcessing();
        dvs.start(path_std, dataset_name);
        rgb.startCapture(path_std);
        if (!rgb.is_recording) {
            throw std::runtime_error("Failed to start RGB capture.");
        }
        uno.start();
        m_livePreviewTimer->start(33);
        setUiState(AppState::Recording);
        const QString realtimeStatus = m_realtimeWorker
            ? "Realtime deblur is active."
            : "Realtime deblur is unavailable. Recording only.";
        view_Deblurred->setText(QString("Recording Dataset: %1...\n%2").arg(datasetInput->text(), realtimeStatus));
    }
    catch (std::exception& e) {
        QMessageBox::critical(this, "Hardware Error", e.what());
        stopRecording();
    }
}

void GUI::stopRecording()
{
    m_livePreviewTimer->stop();
    uno.stop();
    rgb.stopCapture();
    dvs.stopRecord();
    stopRealtimeProcessing();
}

void GUI::updateLivePreview()
{
    cv::Mat temp_bgr_frame;
    rgb.getLatestFrame(&temp_bgr_frame);
    if (temp_bgr_frame.empty()) return;

    cv::Mat temp_rgb_frame;
    cv::cvtColor(temp_bgr_frame, temp_rgb_frame, cv::COLOR_BGR2RGB);
    QImage qimg(temp_rgb_frame.data, temp_rgb_frame.cols, temp_rgb_frame.rows, static_cast<int>(temp_rgb_frame.step), QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(qimg));
}

void GUI::onProcessingProgress(const QString& message)
{
    if (m_blurryFrames.empty()) {
        view_Deblurred->setText(message);
    }
}

void GUI::onBlurryFrameReady(const cv::Mat& frame)
{
    m_blurryFrames.push_back(frame);

    cv::Mat rgbFrame;
    cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
    QImage qimg(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, static_cast<int>(rgbFrame.step), QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(qimg));
}

void GUI::onDeblurredImageReady(int frameIndex, const QImage& image)
{
    if (frameIndex < 0) {
        if (m_currentState == AppState::Recording) {
            view_Deblurred->setPixmap(QPixmap::fromImage(image));
        }
        return;
    }

    cv::Mat mat(image.height(), image.width(), CV_8UC3, const_cast<uchar*>(image.constBits()), image.bytesPerLine());
    if (frameIndex >= static_cast<int>(m_deblurredFrames.size())) {
        m_deblurredFrames.resize(frameIndex + 1);
    }
    m_deblurredFrames[frameIndex] = mat.clone();

    view_Deblurred->setPixmap(QPixmap::fromImage(image));
}

void GUI::onProcessingFinished(bool success)
{
    if (!success) {
        QMessageBox::critical(this, "Processing Failed", "DataProcessor encountered an error.");
        setUiState(AppState::Idle);
        return;
    }

    m_processingCompleted = true;
    view_Deblurred->setText("Preprocess complete. Waiting for TensorRT results...");
    tryEnterPlaybackState();
}

void GUI::onInferenceStreamFinished(int expectedFrameCount)
{
    m_expectedFrameCount = expectedFrameCount;
    m_inferenceCompleted = true;
    tryEnterPlaybackState();
}

void GUI::tryEnterPlaybackState()
{
    if (!m_processingCompleted || !m_inferenceCompleted) {
        return;
    }

    if (m_expectedFrameCount > 0 && static_cast<int>(m_deblurredFrames.size()) < m_expectedFrameCount) {
        m_deblurredFrames.resize(m_expectedFrameCount);
    }
    setupPlayback();
}

void GUI::setupPlayback()
{
    if (m_blurryFrames.empty()) {
        view_Deblurred->setText("Error: No frames available for playback.");
        setUiState(AppState::Idle);
        return;
    }

    if (m_blurryFrames.size() != m_deblurredFrames.size()) {
        qWarning() << "Mismatch in frame counts: Blurry =" << m_blurryFrames.size()
            << "Deblurred =" << m_deblurredFrames.size();
    }

    m_playbackIndex = 0;
    playbackSlider->setRange(0, static_cast<int>(m_blurryFrames.size()) - 1);
    setUiState(AppState::Playback_Paused);
    showFrame(0);
}

void GUI::updatePlayback()
{
    if (m_blurryFrames.empty()) return;

    ++m_playbackIndex;
    if (m_playbackIndex >= static_cast<int>(m_blurryFrames.size())) {
        m_playbackIndex = 0;
    }
    showFrame(m_playbackIndex);
}

void GUI::showFrame(int index)
{
    if (index < 0 || index >= static_cast<int>(m_blurryFrames.size())) return;

    cv::Mat in_rgb;
    cv::cvtColor(m_blurryFrames[index], in_rgb, cv::COLOR_BGR2RGB);
    QImage q_in(in_rgb.data, in_rgb.cols, in_rgb.rows, static_cast<int>(in_rgb.step), QImage::Format_RGB888);
    view_RGB->setPixmap(QPixmap::fromImage(q_in));

    if (index < static_cast<int>(m_deblurredFrames.size()) && !m_deblurredFrames[index].empty()) {
        cv::Mat out_rgb;
        cv::cvtColor(m_deblurredFrames[index], out_rgb, cv::COLOR_BGR2RGB);
        QImage q_out(out_rgb.data, out_rgb.cols, out_rgb.rows, static_cast<int>(out_rgb.step), QImage::Format_RGB888);
        view_Deblurred->setPixmap(QPixmap::fromImage(q_out));
    }
    else {
        view_Deblurred->setText("Frame not deblurred yet / dropped.");
    }

    playbackSlider->blockSignals(true);
    playbackSlider->setValue(index);
    playbackSlider->blockSignals(false);
}

void GUI::closeEvent(QCloseEvent* event)
{
    if (m_currentState == AppState::Recording) {
        stopRecording();
        QThread::msleep(500);
    }
    event->accept();
}
