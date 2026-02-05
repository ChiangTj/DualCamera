#ifndef RGB_H
#define RGB_H

#include <MvCameraControl.h>
#include <opencv2/opencv.hpp>

#include <H5Cpp.h>
#include <QDateTime>
#include <QDir>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "DataQueue.h"
#include "DataStack.h"
#include "ThreadPool.h"

class RGB {
public:
    using FrameReadyCallback = std::function<void(cv::Mat frame, unsigned int frameNumber)>;

    RGB();
    ~RGB();

    void startCapture(const std::string& save_path);
    void stopCapture();
    void getLatestFrame(cv::Mat* output_frame);
    void setFrameReadyCallback(FrameReadyCallback callback);

    bool is_recording = false;

private:
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

    struct ProcessedFrame {
        cv::Mat frame;
        unsigned int frame_number = 0;
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
            const bool success = condition.wait_for(
                lock,
                std::chrono::seconds(timeout_seconds),
                [&]() { return count > 0; });
            if (success) {
                --count;
            }
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

    void initializeInternalParameters();
    bool initializeCameraSDK();
    bool enumerateAndSelectCamera();
    bool allocateImageBuffers();
    bool configureCameraSettings();

    void cleanupResources();
    void clearImageQueue();
    void clearProcessedFrameQueue();
    void clearHDF5Queue();

    static void imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data);
    void distributeTasksThread();
    void processAndQueueFrame(ImageNode* image_node);
    void orderedDispatchLoop();
    void dispatchProcessedFrame(ProcessedFrame* frame);
    void hdf5WriteLoop();

    bool initializeHDF5(const std::string& base_path);
    void extendAndWriteHDF5(ProcessedFrame* frame);
    void closeHDF5();

    ThreadPool* thread_pool = nullptr;
    std::thread task_distribution_thread;
    std::thread ordered_dispatch_thread;
    std::thread hdf5_writer_thread;

    bool task_stop = false;
    bool is_initialized = false;
    bool is_saving = false;
    bool should_exit = false;
    int nRet = MV_OK;
    int frame_counter = 0;
    unsigned int nImageNodeNum = 200;
    std::string save_folder;
    std::atomic<bool> m_workersDone{ false };
    std::atomic<bool> m_dispatchDone{ false };

    void* camera_handle = nullptr;
    unsigned char* rgb_buffer = nullptr;
    unsigned int image_node_count = 200;

    MV_CC_DEVICE_INFO_LIST device_list{};
    MVCC_INTVALUE int_value_params{};
    MV_FRAME_OUT output_frame{};
    MV_CC_PIXEL_CONVERT_PARAM pixel_convert_params{};
    MV_CC_IMAGE image_params{};
    MV_CC_SAVE_IMAGE_PARAM image_save_params{};

    std::mutex task_mutex;
    std::mutex display_mutex;
    std::mutex callback_mutex;
    Semaphore image_semaphore;
    std::vector<std::thread> worker_threads;
    std::queue<std::function<void()>> task_queue;
    std::condition_variable task_cv;
    FrameReadyCallback m_frameReadyCallback;

    DataQueue<ImageNode*> image_queue;
    DataQueue<ProcessedFrame*> processed_frame_queue;
    DataQueue<ProcessedFrame*> hdf5_write_queue;
    LimitedStack<cv::Mat> display_stack{ 3 };

    std::unique_ptr<H5::H5File> h5_file;
    H5::DataSet h5_rgb_dataset;
    H5::DataSet h5_frame_num_dataset;
    hsize_t h5_rgb_dims[4]{};
    std::mutex h5_mutex;

    RGB(const RGB&) = delete;
    RGB& operator=(const RGB&) = delete;
};

#endif // RGB_H
