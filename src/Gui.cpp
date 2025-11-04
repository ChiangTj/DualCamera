#include "Gui.h" 
#include <QDir>

GUI::GUI(QWidget* parent) : QMainWindow(parent) {
	is_running = false;
	setWindowTitle("DualCamera");
	resize(QSize(1920, 920));
	QFont font;
	font.setPointSize(15);
	setFont(font);

	buttonLayout = new QHBoxLayout();
	mainLayout = new QVBoxLayout();
	viewLayout = new QHBoxLayout();
	datasetLayout = new QHBoxLayout();

	view_DVS = new QLabel("DVS Feed (Waiting...)");
	view_RGB = new QLabel("RGB Feed (Waiting...)");

	view_DVS->setMinimumSize(640, 540);
	view_RGB->setMinimumSize(640, 540);
	view_DVS->setScaledContents(true);
	view_RGB->setScaledContents(true);
	view_DVS->setFrameStyle(QFrame::Panel | QFrame::Sunken);
	view_RGB->setFrameStyle(QFrame::Panel | QFrame::Sunken);


	QLabel* datasetLabel = new QLabel(tr("Dataset Name:"));
	datasetInput = new QLineEdit();
	datasetInput->setPlaceholderText("Enter dataset name");
	datasetLayout->addWidget(datasetLabel);
	datasetLayout->addWidget(datasetInput);

	auto openCameraButton = new QPushButton(tr("Open Camera (Start)"));
	auto stopButton = new QPushButton(tr("Stop Record"));
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

	m_dvs_display_timer = new QTimer(this);
	m_rgb_display_timer = new QTimer(this);

	connect(m_dvs_display_timer, &QTimer::timeout, this, &GUI::updateDvsDisplaySlot);
	connect(m_rgb_display_timer, &QTimer::timeout, this, &GUI::updateRgbDisplaySlot);
}

GUI::~GUI() {
	if (is_running) {
		stoprecord();
	}
}

void GUI::start() {
	std::string dataset_name = datasetInput->text().toStdString();
	if (dataset_name.empty()) {
		QMessageBox::warning(this, "Warning", "Please enter a dataset name!");
		return;
	}

	std::string folder_path = "../" + dataset_name;
	QDir().mkpath(QString::fromStdString(folder_path));

	std::lock_guard<std::mutex> lock(mutex);

	if (!is_running) {
		is_running = true;

		dvs.start(dataset_name);
		rgb.startCapture(folder_path);
		uno.start();

		m_dvs_display_timer->start(10);
		m_rgb_display_timer->start(33);

		qDebug() << "Capture started. GUI timers running.";
	}
}

void GUI::stoprecord() {
	std::lock_guard<std::mutex> lock(mutex);

	if (is_running) {
		is_running = false;

		m_dvs_display_timer->stop();
		m_rgb_display_timer->stop();

		uno.stop();
		dvs.stopRecord();
		rgb.stopCapture();

		qDebug() << "Capture stopped. GUI timers stopped.";
		view_DVS->setText("DVS Feed (Stopped)");
		view_RGB->setText("RGB Feed (Stopped)");
	}
}

void GUI::updateDvsDisplaySlot() {
	if (!is_running) return;

	cv::Mat temp;
	temp = dvs.getFrame();

	if (temp.empty()) {
		return;
	}

	cv::Mat frame;
	cv::Size dsize = cv::Size(640, 540);
	cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);

	auto qimg = QImage(frame.data,
		frame.cols,
		frame.rows,
		static_cast<int>(frame.step),
		QImage::Format_RGB888);

	view_DVS->setPixmap(QPixmap::fromImage(qimg));
}


void GUI::updateRgbDisplaySlot() {
	if (!is_running) return;

	cv::Mat temp_bgr_frame;
	rgb.getLatestFrame(&temp_bgr_frame);

	if (temp_bgr_frame.empty()) {
		return;
	}

	cv::Mat resized_bgr_frame;
	cv::Mat display_rgb_frame;
	cv::Size dsize = cv::Size(640, 540);
	cv::resize(temp_bgr_frame, resized_bgr_frame, dsize, 0, 0, cv::INTER_AREA);

	cv::cvtColor(resized_bgr_frame, display_rgb_frame, cv::COLOR_BGR2RGB);

	auto qimg = QImage(display_rgb_frame.data,
		display_rgb_frame.cols,
		display_rgb_frame.rows,
		static_cast<int>(display_rgb_frame.step),
		QImage::Format_RGB888);

	view_RGB->setPixmap(QPixmap::fromImage(qimg));
}

void GUI::closeEvent(QCloseEvent* event) {
	if (is_running) {
		stoprecord();
	}
	event->accept();
}