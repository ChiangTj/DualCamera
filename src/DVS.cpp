#include "../include/DVS.h"
#include <highfive/H5File.hpp>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <queue>

using namespace HighFive;

// ========================
// 辅助数据结构
// ========================
struct EventPacket {
    std::vector<uint16_t> x;
    std::vector<uint16_t> y;
    std::vector<uint64_t> t;
    std::vector<uint8_t> p;
    uint64_t start_ts;
    uint64_t end_ts;
};

// ========================
// 构造函数
// ========================
DVS::DVS() {
    cam = Metavision::Camera::from_first_available();
    cam.get_device().get_facility<Metavision::I_TriggerIn>()
        ->enable(Metavision::I_TriggerIn::Channel::Main);

    camera_width = cam.geometry().width();
    camera_height = cam.geometry().height();

    acc = 20000;
    fps = 50;

    frame_gen = new Metavision::PeriodicFrameGenerationAlgorithm(camera_width, camera_height, acc, fps);
    cd_frame_generator = new Metavision::CDFrameGenerator(camera_width, camera_height);
    cd_frame_generator->set_display_accumulation_time_us(30000);

    // 启动帧生成器回调
    cd_frame_generator->start(
        30,
        [this](const Metavision::timestamp& ts, const cv::Mat& frame) {
            cv::Mat frame_(frame);
            queue.push(frame_);
        }
    );

    // 注册事件回调
    cam.cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        std::lock_guard<std::mutex> lock(event_mutex);
        for (auto it = begin; it != end; ++it) {
            if (current_packet.start_ts == 0)
                current_packet.start_ts = it->t;

            current_packet.x.push_back(it->x);
            current_packet.y.push_back(it->y);
            current_packet.t.push_back(it->t);
            current_packet.p.push_back(it->p);
            current_packet.end_ts = it->t;

            // 如果超过100ms，则打包发送
            if ((it->t - current_packet.start_ts) >= 100000) { // 300ms = 100000us
                event_packets.push(current_packet);
                current_packet = EventPacket();
            }
        }
        });

    stop_flag = false;
    writer_thread = std::thread(&DVS::h5WriterLoop, this);
}

DVS::~DVS() {
    stop();
    if (frame_gen) delete frame_gen;
    if (cd_frame_generator) delete cd_frame_generator;
    stop_flag = true;
    if (writer_thread.joinable()) writer_thread.join();
}

// ========================
// UI帧接口
// ========================
cv::Mat DVS::getFrame() {
    cv::Mat output;
    queue.wait_pop(output);
    return output;
}

// ========================
// 开始采集
// ========================
void DVS::start(const std::string& name) {
    save_folder = "./" + name;
    std::filesystem::create_directories(save_folder);
    cam.start();
}

// ========================
// 停止采集
// ========================
void DVS::stop() {
    cam.stop();
    stop_flag = true;
}

// ========================
// HDF5 写入循环
// ========================
void DVS::h5WriterLoop() {
    while (!stop_flag) {
        EventPacket packet;
        {
            std::unique_lock<std::mutex> lock(event_mutex);
            if (event_packets.empty()) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            packet = event_packets.front();
            event_packets.pop();
        }

        if (packet.x.empty()) continue;

        // 文件名：start_ts.h5
        std::string filename = save_folder + "/" + std::to_string(packet.start_ts) + ".h5";

        try {
            File file(filename, File::Overwrite);

            // 创建分组
            Group events_group = file.createGroup("/events");

            // 写入数据集
            DataSet x_set = file.createDataSet<uint16_t>("/events/x", DataSpace::From(packet.x));
            x_set.write(packet.x);

            DataSet y_set = file.createDataSet<uint16_t>("/events/y", DataSpace::From(packet.y));
            y_set.write(packet.y);

            DataSet t_set = file.createDataSet<uint64_t>("/events/t", DataSpace::From(packet.t));
            t_set.write(packet.t);

            DataSet p_set = file.createDataSet<uint8_t>("/events/p", DataSpace::From(packet.p));
            p_set.write(packet.p);

            // 写入属性
            file.createAttribute<uint64_t>("start_timestamp", DataSpace::From(packet.start_ts)).write(packet.start_ts);
            file.createAttribute<uint64_t>("end_timestamp", DataSpace::From(packet.end_ts)).write(packet.end_ts);

            // 推入h5路径队列，供后续处理
            {
                std::lock_guard<std::mutex> lock(h5_queue_mutex);
                h5_queue.push(filename);
            }
            h5_cv.notify_one();

        }
        catch (const std::exception& e) {
            std::cerr << "[HDF5 Writer] Error: " << e.what() << std::endl;
        }
    }
}

// ========================
// 供外部算法使用的接口
// ========================
std::string DVS::getNextH5() {
    std::unique_lock<std::mutex> lock(h5_queue_mutex);
    h5_cv.wait(lock, [this] { return !h5_queue.empty() || stop_flag; });
    if (h5_queue.empty()) return "";
    std::string path = h5_queue.front();
    h5_queue.pop();
    return path;
}
