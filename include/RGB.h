//#ifndef RGB_H
//#define RGB_H
//
//#include <MvCameraControl.h>
//#include <opencv2/opencv.hpp>
//#include "DataQueue.h"
//#include "DataStack.h"
//#include "ThreadPool.h"
//#include <QDateTime>
//#include <QDir>
//#include <fstream>
//#include <chrono>
//#include <mutex>
//#include <condition_variable>
//#include <thread>
//
//class RGB {
//public:
//    // ==================== Public Interface ====================
//    RGB();
//    ~RGB();
//
//    // Camera control
//    void startCapture(const std::string& save_path);
//    void stopCapture();
//
//    // Image access
//    void getLatestFrame(cv::Mat* output_frame);
//
//    // Status flag
//    bool is_recording;
//
//private:
//    // ==================== Internal Types ====================
//    struct ImageNode {
//        unsigned char* image_data = nullptr;
//        uint64_t data_length = 0;
//        unsigned int width = 0;
//        unsigned int height = 0;
//        unsigned int frame_number = 0;
//        MvGvspPixelType pixel_type = PixelType_Gvsp_BayerGB8;
//
//        ~ImageNode() {
//            if (image_data) {
//                free(image_data);
//                image_data = nullptr;
//            }
//        }
//    };
//
//    class Semaphore {
//    public:
//        explicit Semaphore(long initial_count = 0) : count(initial_count) {}
//        ~Semaphore() { notifyAll(); }
//
//        void wait() {
//            std::unique_lock<std::mutex> lock(mutex);
//            condition.wait(lock, [&]() { return count > 0; });
//            --count;
//        }
//
//        bool wait(int timeout_seconds) {
//            std::unique_lock<std::mutex> lock(mutex);
//            bool success = condition.wait_for(
//                lock,
//                std::chrono::seconds(timeout_seconds),
//                [&]() { return count > 0; }
//            );
//            if (success) --count;
//            return success;
//        }
//
//        void notify() {
//            std::unique_lock<std::mutex> lock(mutex);
//            ++count;
//            condition.notify_one();
//        }
//
//        void notifyAll() {
//            std::unique_lock<std::mutex> lock(mutex);
//            count = 1;
//            condition.notify_all();
//        }
//
//    private:
//        std::mutex mutex;
//        std::condition_variable condition;
//        long count = 0;
//    };
//
//    ThreadPool* thread_pool;
//    std::thread task_distribution_thread;
//
//    void distributeTasksThread();
//    // ==================== Camera State ====================
//    bool task_stop = false;
//    bool is_initialized = false;
//    bool is_saving = false;
//    bool should_exit = false;
//    int nRet;
//    int frame_counter = 0;
//    unsigned int nImageNodeNum;
//    std::string save_folder;
//
//    // ==================== Camera Hardware ====================
//    void* camera_handle = nullptr;
//    unsigned char* rgb_buffer = nullptr;
//    unsigned int image_node_count = 200; // Default buffer count
//
//    // ==================== Camera SDK Structures ====================
//    MV_CC_DEVICE_INFO_LIST device_list;
//    MVCC_INTVALUE int_value_params;
//    MV_FRAME_OUT output_frame;
//    MV_CC_PIXEL_CONVERT_PARAM pixel_convert_params;
//    MV_CC_IMAGE image_params;
//    MV_CC_SAVE_IMAGE_PARAM image_save_params;
//
//    // ==================== Threading ====================
//    std::thread save_thread;
//    std::mutex task_mutex;
//    std::mutex display_mutex;
//    Semaphore image_semaphore;
//    std::vector<std::thread> worker_threads;
//    std::queue<std::function<void()>> task_queue;
//    std::condition_variable task_cv;
//    // ==================== Data Structures ====================
//    DataQueue<ImageNode*> image_queue;
//    LimitedStack<cv::Mat> display_stack{ 3 };
//
//    // ==================== Private Methods ====================
//    // Initialization
//    void initializeInternalParameters();
//    bool initializeCameraSDK();
//    bool enumerateAndSelectCamera();
//    bool allocateImageBuffers();
//    bool configureCameraSettings();
//
//    // Resource management
//    void cleanupResources();
//    void clearImageQueue();
//
//    // Thread functions
//    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);
//    void saveImagesThread();
//    void processAndSaveImage(ImageNode* image_node);
//
//    // Disallow copying
//    RGB(const RGB&) = delete;
//    RGB& operator=(const RGB&) = delete;
//
//
//
//};
//
//#endif // RGB_H

#ifndef RGB_H
#define RGB_H

// 支持 HDF5 C++ API
#include <H5Cpp.h>

#include <MvCameraControl.h>
#include <opencv2/opencv.hpp>
#include "DataQueue.h"
#include "DataStack.h"
#include "ThreadPool.h"
#include <QDateTime>
#include <QDir>
#include <fstream>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <deque>
#include <vector>
#include <string>
#include <functional>

/// RGB 相机类（已扩展以支持多实例与 HDF5 批量保存）
/// 说明：保持与你原来设计的回调 / 线程接口兼容。HDF5 写入使用 HDF5 C++ API（H5::H5File）。
class RGB {
public:
    // ==================== Public Interface ====================
    /// 构造函数：
    /// camera_index: 由外部决定（若 SDK 用索引选择设备，可传入对应索引）
    /// save_path:    保存目录（每个实例独立）
    explicit RGB(int camera_index = 0, const std::string& save_path = "./rgb_cam");
    ~RGB();

        // Camera control
    void startCapture(const std::string& save_path); // 开始采集并设置保存目录
    void stopCapture();                              // 停止采集，确保 flush HDF5 并关闭资源

    // Image access (用于 UI / 预览)
    void getLatestFrame(cv::Mat* output_frame);

    // 状态查询
    bool isRecording() const { return is_recording.load(); }

    // 可选：设置 HDF5 批量大小（默认 100）
    void setHDF5BatchSize(size_t batch) { hdf5_batch_size = (batch > 0 ? batch : 1); }

private:
    // ==================== Internal Types ====================
    struct ImageNode {
        unsigned char* image_data = nullptr;
        uint64_t data_length = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int frame_number = 0;
        uint64_t timestamp_ns = 0; // 纳秒时间戳
        MvGvspPixelType pixel_type = PixelType_Gvsp_BayerGB8;

            ~ImageNode() {
            if (image_data) {
                free(image_data);
                image_data = nullptr;
            }
        }
    };

    // 简单信号量（用于生产者-消费者唤醒）
    class Semaphore {
    public:
        explicit Semaphore(long initial_count = 0) : count(initial_count) {}
        ~Semaphore() { notifyAll(); }

        void wait() {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&]() { return count > 0; });
            --count;
        }

        bool wait(int timeout_seconds) {
            std::unique_lock<std::mutex> lock(mutex);
            bool success = condition.wait_for(
                lock,
                std::chrono::seconds(timeout_seconds),
                [&]() { return count > 0; }
            );
            if (success) --count;
            return success;
        }

        void notify() {
            std::unique_lock<std::mutex> lock(mutex);
            ++count;
            condition.notify_one();
        }

        void notifyAll() {
            std::unique_lock<std::mutex> lock(mutex);
            count = 1;
            condition.notify_all();
        }

    private:
        std::mutex mutex;
        std::condition_variable condition;
        long count = 0;
    };

    // ==================== HDF5 相关 ====================
    // 注意：在链接时需要链接 hdf5_cpp/hdf5 库 (-lhdf5_cpp -lhdf5)
    H5::H5File* h5file = nullptr;
    std::string h5_filename;                 // 当前 HDF5 文件名
    size_t hdf5_batch_size = 100;            // 每次 flush 的帧数（可调整）
    std::deque<cv::Mat> hdf5_image_batch;    // 缓存待写入的图像（BGR 或已转为连续的 uchar 数据）
    std::deque<uint64_t> hdf5_timestamp_batch;
    std::mutex hdf5_mutex;                   // 保护 above batch 的互斥量
    unsigned long long total_frames_written = 0;

    // 将 cv::Mat 批量写入 HDF5（实现放在 cpp 中）
    bool initHDF5File();                     // 创建并打开 HDF5 文件
    void flushHDF5(bool force = false);      // 将 batch 写入 HDF5，并 flush（若 force=true 强制写所有）
    void closeHDF5();                        // 关闭文件并清理

    // ==================== SDK / 硬件 与 状态 ====================
    ThreadPool* thread_pool = nullptr;
    std::thread task_distribution_thread;

    // 状态控制
    std::atomic<bool> task_stop{ false };
    std::atomic<bool> is_initialized{ false };
    std::atomic<bool> is_saving{ false };
    std::atomic<bool> should_exit{ false };
    std::atomic<bool> is_recording{ false };

    int nRet = 0;
    int camera_index = 0;                    // SDK 中用于选择设备的索引或 id
    int frame_counter = 0;
    unsigned int nImageNodeNum = 200;
    std::string save_folder;                 // 保存目录（每个实例独立）

    // ==================== Camera SDK Structures ====================
    void* camera_handle = nullptr;
    unsigned char* rgb_buffer = nullptr;
    unsigned int image_node_count = 200; // Default buffer count

    MV_CC_DEVICE_INFO_LIST device_list;
    MVCC_INTVALUE int_value_params;
    MV_FRAME_OUT output_frame;
    MV_CC_PIXEL_CONVERT_PARAM pixel_convert_params;
    MV_CC_IMAGE image_params;
    MV_CC_SAVE_IMAGE_PARAM image_save_params;

    // ==================== 线程与队列 ====================
    std::thread save_thread;
    std::mutex task_mutex;
    std::mutex display_mutex;
    Semaphore image_semaphore;
    std::vector<std::thread> worker_threads;
    std::queue<std::function<void()>> task_queue;
    std::condition_variable task_cv;

    // 数据结构（和你原来保持一致）
    DataQueue<ImageNode*> image_queue;
    LimitedStack<cv::Mat> display_stack{ 3 };

    // ==================== 私有方法 - 初始化 / 资源管理 / 线程 ====================
    void initializeInternalParameters();
    bool initializeCameraSDK();
    bool enumerateAndSelectCamera(); // 若多相机环境需结合 camera_index 使用
    bool allocateImageBuffers();
    bool configureCameraSettings();

    void cleanupResources();
    void clearImageQueue();

    // 回调与线程函数
    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);
    void distributeTasksThread();     // 从 image_queue 向线程池分发任务
    void saveImagesThread();          // 可用来做 HDF5 定期 flush 的后台线程
    void processAndSaveImage(ImageNode* image_node); // 负责像素格式转换、入 batch、触发 flush

    // 附加：为避免频繁 malloc/free，可在实现中加入对象池接口（待实现）
    // ObjectPool<ImageNode> *image_node_pool;

    // ==================== 禁止拷贝 ====================
    RGB(const RGB&) = delete;
    RGB& operator=(const RGB&) = delete;

};

#endif // RGB_H
