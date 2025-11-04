#ifndef RGB_H
#define RGB_H

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
#include <H5Cpp.h> // +++ ADDED: 引入 HDF5 C++ API
#include <memory>  // +++ ADDED: 引入 smart pointers

class RGB {
public:
    // ==================== Public Interface ====================
    RGB();
    ~RGB();

    // Camera control
    void startCapture(const std::string& save_path);
    void stopCapture();

    // Image access
    void getLatestFrame(cv::Mat* output_frame);

    // Status flag
    bool is_recording;

private:
    // ==================== Internal Types ====================
    struct ImageNode {
        unsigned char* image_data = nullptr;
        uint64_t data_length = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int frame_number = 0;
        MvGvspPixelType pixel_type = PixelType_Gvsp_BayerGB8;

        ~ImageNode() {
            if (image_data) {
                free(image_data);
                image_data = nullptr;
            }
        }
    };

    // +++ ADDED: 新结构体，用于存放已处理好、待写入HDF5的帧
    struct ProcessedFrame {
        cv::Mat frame;       // BGR 格式的 cv::Mat
        unsigned int frame_number;
    };

    // (Semaphore class is unchanged)
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

    ThreadPool* thread_pool;
    std::thread task_distribution_thread;

    void distributeTasksThread();
    // ==================== Camera State ====================
    bool task_stop = false;
    bool is_initialized = false;
    bool is_saving = false;
    bool should_exit = false;
    int nRet;
    int frame_counter = 0;
    unsigned int nImageNodeNum;
    std::string save_folder;

    // ==================== Camera Hardware ====================
    void* camera_handle = nullptr;
    unsigned char* rgb_buffer = nullptr;
    unsigned int image_node_count = 200; // Default buffer count

    // ==================== Camera SDK Structures ====================
    MV_CC_DEVICE_INFO_LIST device_list;
    MVCC_INTVALUE int_value_params;
    MV_FRAME_OUT output_frame;
    MV_CC_PIXEL_CONVERT_PARAM pixel_convert_params;
    MV_CC_IMAGE image_params;
    MV_CC_SAVE_IMAGE_PARAM image_save_params;

    // ==================== Threading ====================
    // std::thread save_thread; // <<< CHANGED: 我们将用 hdf5_writer_thread 替换
    std::thread hdf5_writer_thread; // +++ ADDED: 专用的HDF5写入线程
    std::mutex task_mutex;
    std::mutex display_mutex;
    Semaphore image_semaphore;
    std::vector<std::thread> worker_threads;
    std::queue<std::function<void()>> task_queue;
    std::condition_variable task_cv;

    // ==================== Data Structures ====================
    DataQueue<ImageNode*> image_queue; // L1 (Callback) -> L2 (Distributor) 的队列
    DataQueue<ProcessedFrame*> hdf5_write_queue; // +++ ADDED: L3 (Pool) -> L4 (HDF5 Writer) 的队列
    LimitedStack<cv::Mat> display_stack{ 3 };

    // ==================== HDF5 Members ====================
    std::unique_ptr<H5::H5File> h5_file; // +++ ADDED: HDF5 文件句柄
    H5::DataSet h5_rgb_dataset;         // +++ ADDED: HDF5 图像数据集
    hsize_t h5_rgb_dims[4];             // +++ ADDED: 图像数据集维度
    std::mutex h5_mutex;                // +++ ADDED: 保护HDF5文件操作（主要在开关时）

    // ==================== Private Methods ====================
    // Initialization
    void initializeInternalParameters();
    bool initializeCameraSDK();
    bool enumerateAndSelectCamera();
    bool allocateImageBuffers();
    bool configureCameraSettings();

    // Resource management
    void cleanupResources();
    void clearImageQueue();
    void clearHDF5Queue();

    // Thread functions
    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);

    // <<< CHANGED: 这是线程池的新任务
    void processAndQueueFrame(ImageNode* image_node); // +++ ADDED

    // <<< REPLACED: 旧的保存函数 (将在 .cpp 中被 processAndQueueFrame 替换)
    // void processAndSaveImage(ImageNode* image_node); 

    // +++ ADDED: HDF5 写入线程的主循环
    void hdf5WriteLoop();

    // +++ ADDED: HDF5 辅助函数
    bool initializeHDF5(const std::string& base_path);
    void extendAndWriteHDF5(ProcessedFrame* frame);
    void closeHDF5();

    // Disallow copying
    RGB(const RGB&) = delete;
    RGB& operator=(const RGB&) = delete;

};

#endif // RGB_H