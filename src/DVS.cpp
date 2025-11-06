#include "../include/DVS.h" // 假设 .h 文件在 include 目录

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

    // *** 核心修改 (1): 更改回调函数 ***
    // 启动帧生成器，并设置回调函数
    cd_frame_generator->start(
        30, // 以大约 30 fps 调用回调

        // [旧的回调，导致卡死]
        // [this](const Metavision::timestamp& ts, const cv::Mat& frame) {
        //     cv::Mat frame_(frame);
        //     queue.push(frame_); // <-- 之前是推入阻塞队列
        // }

        // [新的回调，非阻塞]
        [this](const Metavision::timestamp& ts, const cv::Mat& frame) {
            // 当新帧生成时，锁定互斥锁
            std::lock_guard<std::mutex> lock(m_frame_mutex);
            // 将新帧深拷贝 (clone) 到 m_latest_frame 成员变量
            m_latest_frame = frame.clone();
        });

    // 注册 CD 事件回调：当相机捕获到事件数据时，将其传入帧生成器
    // (这部分保持不变)
    cam.cd().add_callback([&](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        frame_gen->process_events(begin, end);      // 用于周期性帧生成
        cd_frame_generator->add_events(begin, end);  // 用于 CD 帧生成

        // (注意：你之前的代码没有将事件推入 raw_queue，
        // 如果你需要保存原始事件，你可能需要在这里添加 raw_queue.push(...))
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
    // [旧的实现，导致卡死]
    // cv::Mat output;
    // queue.wait_pop(output); // <-- 阻塞 GUI 线程
    // return output;

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
    // (互斥锁在此处自动释放)

    // 立即返回 output。
    // 如果 m_latest_frame 还没有准备好，它将返回一个空 Mat。
    // 你的 Gui.cpp 中的 updateDvsDisplaySlot() 已经有了
    // if (temp.empty()) 的检查，所以这是安全的。
    return output;
}

// 开始采集与录制
// (保持不变)
void DVS::start(const std::string& name) {
    // 设置保存文件路径，保存为 raw 格式
    save_folder = "./" + name + "/" + name + ".raw";

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