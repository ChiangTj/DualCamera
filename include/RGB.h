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
    std::thread save_thread;
    std::mutex task_mutex;
    std::mutex display_mutex;
    Semaphore image_semaphore;
    std::vector<std::thread> worker_threads;
    std::queue<std::function<void()>> task_queue;
    std::condition_variable task_cv;
    // ==================== Data Structures ====================
    DataQueue<ImageNode*> image_queue;
    LimitedStack<cv::Mat> display_stack{ 3 };

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

    // Thread functions
    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);
    void saveImagesThread();
    void processAndSaveImage(ImageNode* image_node);

    // Disallow copying
    RGB(const RGB&) = delete;
    RGB& operator=(const RGB&) = delete;



};

#endif // RGB_H