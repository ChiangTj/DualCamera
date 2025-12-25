#ifndef RGB_H
#define RGB_H

#include <H5Cpp.h>
#include <MvCameraControl.h>
#include <opencv2/opencv.hpp>
#include "DataQueue.h"
#include "DataStack.h"
#include "ThreadPool.h"
#include <QDateTime>
#include <QDir>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>

class RGB {
public:
    RGB(int camera_index, std::string camera_name, const std::string& save_path);
    ~RGB();

    void startCapture(const std::string& save_path);
    void stopCapture();
    void getLatestFrame(cv::Mat* output_frame);
    bool setExposureTime(float exposure_time_us);
    bool setGain(float gain_value);
    bool isRecording() const { return is_recording.load(); }
    std::string getName() const { return camera_name; }

private:
    struct ImageNode {
        unsigned char* image_data = nullptr;
        uint64_t data_length = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int frame_number = 0;
        MvGvspPixelType pixel_type = PixelType_Gvsp_BayerGB8;
        ~ImageNode() { if (image_data) free(image_data); }
    };

    struct ProcessedFrame {
        cv::Mat frame;
        unsigned int frame_number;
    };

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
            bool success = condition.wait_for(lock, std::chrono::seconds(timeout_seconds), [&]() { return count > 0; });
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

    int camera_index;
    std::string camera_name;
    std::string save_folder;
    void* camera_handle = nullptr;
    unsigned char* rgb_buffer = nullptr;
    int nRet = 0;
    unsigned int nImageNodeNum = 200;
    int frame_counter = 0; // [已修复] 添加缺失的成员变量

    MV_CC_DEVICE_INFO_LIST device_list;
    MVCC_INTVALUE int_value_params;
    MV_FRAME_OUT output_frame;
    MV_CC_PIXEL_CONVERT_PARAM pixel_convert_params;
    MV_CC_IMAGE image_params;
    MV_CC_SAVE_IMAGE_PARAM image_save_params;

    ThreadPool* thread_pool = nullptr;
    std::thread task_distribution_thread;
    std::thread hdf5_writer_thread;
    std::mutex task_mutex;
    std::mutex display_mutex;
    Semaphore image_semaphore;

    std::atomic<bool> task_stop{ false };
    std::atomic<bool> is_initialized{ false };
    std::atomic<bool> is_saving{ false };
    std::atomic<bool> should_exit{ false };
    std::atomic<bool> is_recording{ false };

    DataQueue<ImageNode*> image_queue;
    DataQueue<ProcessedFrame*> hdf5_write_queue;
    LimitedStack<cv::Mat> display_stack{ 3 };

    std::unique_ptr<H5::H5File> h5_file;
    H5::DataSet h5_rgb_dataset;
    hsize_t h5_rgb_dims[4];
    std::mutex h5_mutex;

    // 静态成员：保证 SDK 全局只初始化一次
    static std::atomic<bool> global_sdk_initialized;
    static std::mutex global_sdk_mutex;

    void initializeInternalParameters();
    bool initializeCameraSDK();
    bool enumerateAndSelectCamera();
    bool allocateImageBuffers();
    bool configureCameraSettings();
    void cleanupResources();
    void clearImageQueue();
    void clearHDF5Queue();
    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);
    void distributeTasksThread();
    void processAndQueueFrame(ImageNode* image_node);
    void hdf5WriteLoop();
    bool initializeHDF5(const std::string& base_path);
    void extendAndWriteHDF5(ProcessedFrame* frame);
    void closeHDF5();

    RGB(const RGB&) = delete;
    RGB& operator=(const RGB&) = delete;
};

#endif // RGB_H