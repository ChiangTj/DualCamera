#include "../include/DVS.h"

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
    cd_frame_generator->set_display_accumulation_time_us(30000);  // 设置显示的事件累积时间窗口 (us)

    // 启动帧生成器，并设置回调函数，将生成的帧放入队列中
    cd_frame_generator->start(
        30, // 以大约 30 fps 调用回调
        [this](const Metavision::timestamp& ts, const cv::Mat& frame) {
            cv::Mat frame_(frame);  // 拷贝一份 OpenCV Mat 图像
            queue.push(frame_);     // 推入线程安全队列，供 getFrame() 获取
        });

    // 注册 CD 事件回调：当相机捕获到事件数据时，将其传入帧生成器
    cam.cd().add_callback([&](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        frame_gen->process_events(begin, end);        // 用于周期性帧生成
        cd_frame_generator->add_events(begin, end);   // 用于 CD 帧生成
        });
}

// 析构函数：释放资源
DVS::~DVS() {
    if (cam.is_running()) {
        cam.stop();  // 停止相机采集
    }
    if (frame_gen)
        delete frame_gen;  // 释放周期性帧生成器
    if (cd_frame_generator)
        delete cd_frame_generator;  // 释放事件帧生成器
}

// 获取一帧图像（从队列中取出一帧事件累积生成的图像）
cv::Mat DVS::getFrame() {
    cv::Mat output;
    queue.wait_pop(output);  // 阻塞等待，直到有新帧被推入队列
    return output;
}

// 开始采集与录制
void DVS::start(const std::string& name) {
    // 设置保存文件路径，保存为 raw 格式
    save_folder = "./" + name + "/" + name + ".raw";

    cam.start();                        // 启动相机数据流
    cam.start_recording(save_folder);   // 开始录制事件数据到指定路径
}

// 停止相机采集
void DVS::stop() {
    cam.stop();
}

// 停止录制并关闭相机
void DVS::stopRecord() {
    cam.stop_recording();  // 停止录制
    cam.stop();            // 停止相机
}
