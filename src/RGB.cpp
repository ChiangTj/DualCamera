#include "../include/RGB.h"
#include <QDir>
#include <opencv2/opencv.hpp>
#include <mutex>

// =============================================
// Initialization and Cleanup
// =============================================

void RGB::initializeInternalParameters()
{
    // Reset counters and flags
    frame_counter = 0;
    is_saving = false;
    should_exit = false;
    is_initialized = false;
    is_recording = false;

    // Clear data structures
    image_queue.clear();

    // Initialize camera parameters
    nRet = MV_OK;
    camera_handle = nullptr;
    nImageNodeNum = 200;

    // Initialize buffers
    rgb_buffer = nullptr;

    // Initialize camera SDK structures
    memset(&device_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    memset(&pixel_convert_params, 0, sizeof(MV_CC_PIXEL_CONVERT_PARAM));
    memset(&output_frame, 0, sizeof(MV_FRAME_OUT));
    memset(&image_save_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM));
    memset(&image_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM));
    memset(&int_value_params, 0, sizeof(MVCC_INTVALUE));

    thread_pool = nullptr;
}

RGB::RGB()
{
    initializeInternalParameters();

    // Step 1: Initialize camera SDK
    if (!initializeCameraSDK()) return;

    // Step 2: Enumerate and select camera device
    if (!enumerateAndSelectCamera()) return;

    // Step 3: Allocate image buffers
    if (!allocateImageBuffers()) return;

    // Step 4: Configure camera settings
    if (!configureCameraSettings()) return;

    is_initialized = true;
    printf("RGB camera initialized successfully.\n");
}

RGB::~RGB()
{
    stopCapture();
    cleanupResources();
}

// =============================================
// Camera Initialization Helpers
// =============================================

//初始化程序
bool RGB::initializeCameraSDK()
{
    nRet = MV_CC_Initialize();
    if (MV_OK != nRet) {
        printf("Failed to initialize SDK! Error: [0x%x]\n", nRet);
        return false;
    }
    return true;
}

//寻找设备
bool RGB::enumerateAndSelectCamera()
{
    nRet = MV_CC_EnumDevices(MV_GENTL_CXP_DEVICE, &device_list);
    if (MV_OK != nRet) {
        printf("Failed to enumerate devices! Error: [0x%x]\n", nRet);
        return false;
    }

    if (device_list.nDeviceNum == 0) {
        printf("No compatible cameras found!\n");
        return false;
    }
    //创建相机的句柄
    nRet = MV_CC_CreateHandle(&camera_handle, device_list.pDeviceInfo[0]);
    if (MV_OK != nRet) {
        printf("Failed to create camera handle! Error: [0x%x]\n", nRet);
        return false;
    }
    //打开设备
    nRet = MV_CC_OpenDevice(camera_handle);
    if (MV_OK != nRet) {
        printf("Failed to open camera device! Error: [0x%x]\n", nRet);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
        return false;
    }

    return true;
}


//创建临时的图像缓存
bool RGB::allocateImageBuffers()
{
    // Get image dimensions from camera
    MVCC_INTVALUE width = { 0 }, height = { 0 };
    MV_CC_GetIntValue(camera_handle, "Width", &width);
    MV_CC_GetIntValue(camera_handle, "Height", &height);

    // Allocate RGB buffer
    size_t buffer_size = width.nCurValue * height.nCurValue * 3;
    rgb_buffer = (unsigned char*)malloc(buffer_size);
    if (rgb_buffer == nullptr) {
        printf("Failed to allocate memory for RGB buffer!\n");
        cleanupResources();
        return false;
    }

    return true;
}

bool RGB::configureCameraSettings()
{
    //设置触发方式
    // Configure trigger settings
    const std::vector<std::tuple<const char*, int, const char*>> settings = {
        {"TriggerMode", 1, "Trigger Mode"},
        {"TriggerSource", 0, "Trigger Source"},
        {"TriggerActivation", 0, "Trigger Activation"}, // Rising edge trigger
        {"OverlapMode", 1, "Overlap Mode"}
    };

    for (const auto& [name, value, description] : settings) {
        nRet = MV_CC_SetEnumValue(camera_handle, name, value);
        if (MV_OK != nRet) {
            printf("Failed to set %s! Error: [0x%x]\n", description, nRet);
            cleanupResources();
            return false;
        }
    }

    // Set image node number
    nRet = MV_CC_SetImageNodeNum(camera_handle, nImageNodeNum);
    if (MV_OK != nRet) {
        printf("Failed to set image node number! Error: [0x%x]\n", nRet);
        cleanupResources();
        return false;
    }

    return true;
}

// =============================================
// Resource Management
// =============================================

void RGB::cleanupResources()
{
    // Release image buffer
    if (rgb_buffer != nullptr) {
        free(rgb_buffer);
        rgb_buffer = nullptr;
    }

    // Close camera handle
    if (camera_handle != nullptr) {
        MV_CC_CloseDevice(camera_handle);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
    }

    // Clear display stack
    std::lock_guard<std::mutex> lock(display_mutex);
    display_stack.clear();
}

void RGB::clearImageQueue()
{
    ImageNode* node = nullptr;
    while (image_queue.try_pop(node)) {
        if (node) {
            if (node->image_data) {
                free(node->image_data);
            }
            delete node;
        }
    }
}

// =============================================
// Camera Control
// =============================================

void RGB::startCapture(const std::string& save_path)
{
    if (!is_initialized || camera_handle == nullptr) {
        printf("Camera not properly initialized. Cannot start capture.\n");
        return;
    }

    // Create thread pool with worker threads (adjust as needed) 使用线程池的方式提高图片的存储效率
    // 这里使用6个线程，可以根据实际需求进行调整
    const size_t num_threads = 6;
    thread_pool = new ThreadPool(num_threads);

    // Register image callback  使用回调函数imageCallback获得元数据 
    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
    if (MV_OK != nRet) {
        printf("Failed to register image callback! Error: [0x%x]\n", nRet);
        return;
    }

    // Start image acquisition 开始取流
    nRet = MV_CC_StartGrabbing(camera_handle);
    if (MV_OK != nRet) {
        printf("Failed to start image grabbing! Error: [0x%x]\n", nRet);
        return;
    }

    // Create save directory
    save_folder = save_path + "/rgb/";
    QDir dir;
    if (!dir.mkpath(QString::fromStdString(save_folder))) {
        printf("Failed to create save directory: %s\n", save_folder.c_str());
        MV_CC_StopGrabbing(camera_handle);
        return;
    }

    // Set flags
    is_saving = true;  
    should_exit = false;

    // Start task distribution thread 启动独立的保存进程distributeTasksThread
    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);

    printf("RGB Camera started successfully with %zu worker threads!\n", num_threads);
}

void RGB::stopCapture()
{
    // Signal threads to exit
    should_exit = true;

    // Stop save thread
    if (is_saving) {
        is_saving = false;
        image_semaphore.notify(); // Wake up save thread if waiting

        if (save_thread.joinable()) {
            save_thread.join();
        }
    }

    // Stop camera acquisition
    if (camera_handle != nullptr) {
        nRet = MV_CC_StopGrabbing(camera_handle);
        if (MV_OK != nRet) {
            printf("Failed to stop image grabbing! Error: [0x%x]\n", nRet);
        }
    }

    // Clear image queue
    clearImageQueue();
}

// =============================================
// Image Processing
// =============================================

void RGB::getLatestFrame(cv::Mat* output_frame)
{
    std::lock_guard<std::mutex> lock(display_mutex);

    cv::Mat latest_frame;
    bool success = display_stack.top(latest_frame);

    if (success && !latest_frame.empty()) {
        *output_frame = latest_frame.clone();
        cv::cvtColor(*output_frame, *output_frame, cv::COLOR_RGB2BGR);
    }
}

// =============================================
// Thread Functions
// =============================================

//获得元数据，并且将元数据推入队列中
void RGB::imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data)
{
    if (!frame_info || !image_data || !user_data) return;

    RGB* camera = static_cast<RGB*>(user_data);
    if (camera->should_exit) return;

    // Create new image node
    //ImageNode是一个结构体
    ImageNode* image_node = new ImageNode();
    if (!image_node) {
        printf("Failed to allocate memory for image node.\n");
        return;
    }

    // Populate image node
    //获得基础的信息
    image_node->pixel_type = frame_info->enPixelType;
    image_node->data_length = frame_info->nFrameLenEx;
    image_node->width = frame_info->nWidth;
    image_node->height = frame_info->nHeight;
    image_node->frame_number = frame_info->nFrameNum;

    // Copy image data
    image_node->image_data = (unsigned char*)malloc(image_node->data_length);
    if (!image_node->image_data) {
        printf("Failed to allocate memory for image data.\n");
        delete image_node;
        return;
    }
    
    memcpy(image_node->image_data, image_data, image_node->data_length);

    // Add to processing queue
    //推入队列
    camera->image_queue.push(image_node);
    camera->image_semaphore.notify();
}


void RGB::distributeTasksThread()
{
    while (is_saving || !image_queue.empty()) {
        image_semaphore.wait();
        if (should_exit && image_queue.empty()) {
            break;
        }

        ImageNode* image_node = nullptr;
        if (!image_queue.try_pop(image_node) || image_node == nullptr) continue;
        // Submit the image processing task to the thread pool
        //在线程池找寻空闲的保存线程，分配给队列中的第一个数据
        if (thread_pool) {
            thread_pool->enqueue([this, image_node]() {
                processAndSaveImage(image_node);
                delete image_node; // Now safe to delete in worker thread
                });
        }
        else {
            // Fallback if thread pool isn't available
            processAndSaveImage(image_node);
            delete image_node;
        }
    }

    printf("Task distribution thread exited.\n");
}

//保存操作
void RGB::processAndSaveImage(ImageNode* image_node)
{
    // Allocate RGB buffer for conversion
    size_t rgb_buffer_size = image_node->width * image_node->height * 3;
    unsigned char* local_rgb_buffer = (unsigned char*)malloc(rgb_buffer_size);
    if (!local_rgb_buffer) {
        printf("Failed to allocate memory for RGB conversion.\n");
        return;
    }

    // Convert pixel format
    MV_CC_PIXEL_CONVERT_PARAM convert_params = { 0 };
    convert_params.enSrcPixelType = image_node->pixel_type;
    convert_params.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
    convert_params.nWidth = image_node->width;
    convert_params.nHeight = image_node->height;
    convert_params.nSrcDataLen = image_node->data_length;
    convert_params.pSrcData = image_node->image_data;
    convert_params.pDstBuffer = local_rgb_buffer;
    convert_params.nDstBufferSize = rgb_buffer_size;

    int result = MV_CC_ConvertPixelType(camera_handle, &convert_params);
    if (MV_OK != result) {
        printf("Failed to convert pixel type! Error: [0x%x]\n", result);
        free(local_rgb_buffer);
        return;
    }

    // Create OpenCV image from raw buffer
    cv::Mat image(image_node->height, image_node->width, CV_8UC3, local_rgb_buffer);

    // Make deep copy immediately
    cv::Mat image_copy = image.clone();  // Deep copy -- safe to keep after freeing local_rgb_buffer

    // Now we can safely release the raw RGB buffer
    free(local_rgb_buffer);
    local_rgb_buffer = nullptr;

    // Free the original image data
    free(image_node->image_data);
    image_node->image_data = nullptr;

    //// Push to display stack (thread-safe)
    //{
    //    std::lock_guard<std::mutex> lock(display_mutex);
    //    display_stack.push(image_copy);
    //}

    // Save image to file using safe copy
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/frame_%06d.png",
        save_folder.c_str(), image_node->frame_number);

    if (!cv::imwrite(filename, image_copy)) {
        printf("Failed to save image: %s\n", filename);
    }
}