#include "../include/Gui.h"

GUI::GUI(QWidget* parent) : QMainWindow(parent) {
    //创建窗口
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

    view_DVS = new QLabel();

    view_RGB = new QLabel();
    //输入数据集名称的文本框
    QLabel* datasetLabel = new QLabel(tr("Dataset Name:"));
    datasetInput = new QLineEdit();
    datasetInput->setPlaceholderText("Enter dataset name");
    datasetLayout->addWidget(datasetLabel);
    datasetLayout->addWidget(datasetInput);
    //创建开始录制和停止的窗口
    auto openCameraButton = new QPushButton(tr("open camera"));
    auto stopButton = new QPushButton(tr("stop record"));
    stopButton->setCheckable(true);
    auto mainWidget = new QWidget();
    
    buttonLayout->addWidget(openCameraButton);
    buttonLayout->addWidget(stopButton);
    viewLayout->addWidget(view_DVS);
    viewLayout->addWidget(view_RGB);

    mainLayout->addLayout(viewLayout);
    mainLayout->addLayout(datasetLayout);
    mainLayout->addLayout(buttonLayout);
    mainWidget->setLayout(mainLayout);
    setCentralWidget(mainWidget);

    //连接到两个按钮
    connect(openCameraButton, &QPushButton::clicked, this, &GUI::start);

    connect(stopButton, &QPushButton::clicked, this,
        &GUI::stoprecord);
}

//启动录制
void GUI::start() {
    //创建数据集文件夹
    std::string dataset_name = datasetInput->text().toStdString();
    if (dataset_name.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter a dataset name!");
        return;
    }
    std::string folder_path = "./" + dataset_name;
    QDir().mkpath(QString::fromStdString(folder_path));

    std::lock_guard<std::mutex> lock(mutex);
    //is_running为程序运行标志
    if (!is_running) {
        is_running = true;
        dvs.start(dataset_name);//dvs开始录制
        rgb.startCapture(folder_path);//RGB开始录制
        uno.start();//单片机开始输出方波控制信号
        //分离DVS和RGB的线程
        DVS_thread = std::thread(&GUI::updateDVS, this);
        RGB_thread = std::thread(&GUI::updateRGB, this);
        DVS_thread.detach();
        RGB_thread.detach();
    }
}

//实时显示DVS画面 
void GUI::updateDVS() {

    cv::Size dsize = cv::Size(640, 540);
    cv::Mat temp;
    cv::Mat frame;
    while (is_running) {
        temp = dvs.getFrame(); //dvs路会通过累积的方式获得帧
        cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);
        auto qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
        view_DVS->setPixmap(QPixmap::fromImage(qimg).scaledToWidth(view_DVS->width()));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

//实时显示RGB路图像，但是存在一些问题，会卡死、
void GUI::updateRGB() {
    cv::Mat frame;
    cv::Mat temp;
    cv::Size dsize = cv::Size(640, 540);

    while (is_running) {
        rgb.getLatestFrame(&temp);
        if (!temp.empty()) {
            cv::resize(temp, frame, dsize, 0, 0, cv::INTER_AREA);
            auto qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
            view_RGB->setPixmap(QPixmap::fromImage(qimg).scaledToWidth(view_RGB->width()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));  // 控制约30fps
    }
}
//程序结束
void GUI::stoprecord() {
    uno.stop();
    dvs.stopRecord();
    rgb.stopCapture();

}

GUI::~GUI() {
}

void GUI::closeEvent(QCloseEvent* event) {
    _exit(0);
}
