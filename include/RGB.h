#ifndef RGB_H
#define RGB_H

// 支持 HDF5 C++ API (需在 CMakeLists.txt 中链接 hdf5_cpp)
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

class RGB {
public:
    // ==================== Public Interface ====================

    /// 构造函数
    /// @param camera_index : 相机索引 (0 或 1), 取决于系统枚举顺序
    /// @param camera_name  : 相机标识 ("left" 或 "right"), 用于生成文件名
    /// @param save_path    : 数据保存的根目录
    RGB(int camera_index, std::string camera_name, const std::string& save_path);
    ~RGB();

    // --- 采集控制 ---
    void startCapture(const std::string& save_path); // 开始采集
    void stopCapture();                              // 停止采集并安全关闭 HDF5

    // --- 图像访问 (用于 UI 预览) ---
    void getLatestFrame(cv::Mat* output_frame);

    // --- 参数设置 (单位: us / float) ---
    // 建议在 startCapture 前调用
    bool setExposureTime(float exposure_time_us);
    bool setGain(float gain_value);

    // --- 状态与配置 ---
    bool isRecording() const { return is_recording.load(); }
    std::string getName() const { return camera_name; }

    // 设置 HDF5 批量写入大小 (默认 100 帧写一次磁盘)
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

    // 简单的信号量实现
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
    H5::H5File* h5file = nullptr;
    std::string h5_filename;
    size_t hdf5_batch_size = 100;
    std::deque<cv::Mat> hdf5_image_batch;      // 待写入图像缓存
    std::deque<uint64_t> hdf5_timestamp_batch; // 待写入时间戳缓存
    std::mutex hdf5_mutex;
    unsigned long long total_frames_written = 0;

    bool initHDF5File();                // 初始化 HDF5 文件
    void flushHDF5(bool force = false); // 刷入磁盘
    void closeHDF5();                   // 关闭文件

    // ==================== SDK / 硬件 与 状态 ====================
    ThreadPool* thread_pool = nullptr;
    std::thread task_distribution_thread;

    // 状态原子变量
    std::atomic<bool> task_stop{ false };
    std::atomic<bool> is_initialized{ false };
    std::atomic<bool> is_saving{ false };
    std::atomic<bool> should_exit{ false };
    std::atomic<bool> is_recording{ false };

    int nRet = 0;
    int camera_index = 0;          // 硬件设备索引 (0, 1...)
    std::string camera_name;       // 逻辑名称 ("left", "right")
    int frame_counter = 0;
    unsigned int nImageNodeNum = 200;
    std::string save_folder;

    void* camera_handle = nullptr;
    unsigned char* rgb_buffer = nullptr; // 临时转换缓存

    // SDK 参数结构体
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

    // 数据队列
    DataQueue<ImageNode*> image_queue;
    LimitedStack<cv::Mat> display_stack{ 3 };

    // ==================== Private Methods ====================
    void initializeInternalParameters();
    bool initializeCameraSDK();
    bool enumerateAndSelectCamera(); // 根据 camera_index 打开设备
    bool allocateImageBuffers();
    bool configureCameraSettings();

    void cleanupResources();
    void clearImageQueue();

    // 回调函数
    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);

    // 线程任务
    void distributeTasksThread();     // 任务分发
    void saveImagesThread();          // 定时保存检查
    void processAndSaveImage(ImageNode* image_node); // 核心处理逻辑

    // 禁止拷贝
    RGB(const RGB&) = delete;
    RGB& operator=(const RGB&) = delete;
};

#endif // RGB_H