#include <QString>
#include <QFileInfo>
#include "DVS.h" // 假设 .h 文件在 include 目录

// 构造函数：初始化 DVS 相机并配置相关模块
DVS::DVS() {
    // 从系统中找到第一个可用的 Metavision 相机
    cam = Metavision::Camera::from_first_available();

    // 启用外部触发输入通道（用于接收外部触发信号）
    cam.get_device().get_facility<Metavision::I_TriggerIn>()->enable(Metavision::I_TriggerIn::Channel::Main);

    // 获取相机分辨率
    camera_width = cam.geometry().width();
    camera_height = cam.geometry().height();

    // 设置帧生成的参数
    acc = 20000;   // 累积时间 (us)，控制多少时间内的事件被用来生成一帧
    fps = 50;      // 输出帧率 (frames per second)

    // 创建周期性帧生成器，用于将事件数据转换为固定帧率的视频帧
    frame_gen = new Metavision::PeriodicFrameGenerationAlgorithm(camera_width, camera_height, acc, fps);

    // 创建事件帧生成器（CDFrameGenerator），将事件转换为 OpenCV 的图像
    cd_frame_generator = new Metavision::CDFrameGenerator(camera_width, camera_height);
    cd_frame_generator->set_display_accumulation_time_us(30000); // 设置显示的事件累积时间窗口 (us)

    cd_frame_generator->start(
        30, // 以大约 30 fps 调用回调

        // [新的回调，非阻塞]
        [this](const Metavision::timestamp& ts, const cv::Mat& frame) {
            if (m_frame_mutex.try_lock())
            {
                m_latest_frame = frame.clone();
                m_frame_mutex.unlock();
            }
        });

    cam.cd().add_callback([&](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        frame_gen->process_events(begin, end);      // 用于周期性帧生成
        cd_frame_generator->add_events(begin, end);  // 用于 CD 帧生成

        });
}

// 析构函数：释放资源
DVS::~DVS() {
    if (cam.is_running()) {
        cam.stop(); // 停止相机采集
    }
    if (frame_gen)
        delete frame_gen; // 释放周期性帧生成器
    if (cd_frame_generator)
        delete cd_frame_generator; // 释放事件帧生成器
}

// *** 核心修改 (2): 更改 getFrame() 函数 ***
// 获取一帧图像（从 m_latest_frame 非阻塞地获取）
cv::Mat DVS::getFrame() {

    // [新的实现，非阻塞]
    cv::Mat output; // 创建一个空的 Mat

    // 创建一个临时的作用域，以便 lock_guard 及时释放锁
    {
        std::lock_guard<std::mutex> lock(m_frame_mutex);

        // 检查 m_latest_frame 是否已经被填充
        if (!m_latest_frame.empty()) {
            // 深拷贝 (clone) 最新的帧到 output
            output = m_latest_frame.clone();
        }
    }
    return output;
}

// DVS.cpp (新的，推荐)
void DVS::start(const std::string& segment_folder_path) {
    // 提取 segment_name (例如 "segment_1")
    std::string segment_name = QFileInfo(QString::fromStdString(segment_folder_path)).fileName().toStdString();

    // 设置保存文件路径，保存为 raw 格式
    // (我们假设 C++ 已经创建了 segment_folder_path 目录)
    save_folder = segment_folder_path + "/" + segment_name + ".raw";

    cam.start(); // 启动相机数据流
    cam.start_recording(save_folder); // 开始录制事件数据到指定路径
}

// 停止相机采集
// (保持不变)
void DVS::stop() {
    cam.stop();
}

// 停止录制并关闭相机
// (保持不变)
void DVS::stopRecord() {
    cam.stop_recording(); // 停止录制
    cam.stop();           // 停止相机
}