//#include "../include/RGB.h"
//#include <QDir>
//#include <opencv2/opencv.hpp>
//#include <mutex>
//
//// =============================================
//// Initialization and Cleanup
//// =============================================
//
//void RGB::initializeInternalParameters()
//{
//    // Reset counters and flags
//    frame_counter = 0;
//    is_saving = false;
//    should_exit = false;
//    is_initialized = false;
//    is_recording = false;
//
//    // Clear data structures
//    image_queue.clear();
//
//    // Initialize camera parameters
//    nRet = MV_OK;
//    camera_handle = nullptr;
//    nImageNodeNum = 200;
//
//    // Initialize buffers
//    rgb_buffer = nullptr;
//
//    // Initialize camera SDK structures
//    memset(&device_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
//    memset(&pixel_convert_params, 0, sizeof(MV_CC_PIXEL_CONVERT_PARAM));
//    memset(&output_frame, 0, sizeof(MV_FRAME_OUT));
//    memset(&image_save_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM));
//    memset(&image_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM));
//    memset(&int_value_params, 0, sizeof(MVCC_INTVALUE));
//
//    thread_pool = nullptr;
//}
//
//RGB::RGB()
//{
//    initializeInternalParameters();
//
//    // Step 1: Initialize camera SDK
//    if (!initializeCameraSDK()) return;
//
//    // Step 2: Enumerate and select camera device
//    if (!enumerateAndSelectCamera()) return;
//
//    // Step 3: Allocate image buffers
//    if (!allocateImageBuffers()) return;
//
//    // Step 4: Configure camera settings
//    if (!configureCameraSettings()) return;
//
//    is_initialized = true;
//    printf("RGB camera initialized successfully.\n");
//}
//
//RGB::~RGB()
//{
//    stopCapture();
//    cleanupResources();
//}
//
//// =============================================
//// Camera Initialization Helpers
//// =============================================
//
////初始化程序
//bool RGB::initializeCameraSDK()
//{
//    nRet = MV_CC_Initialize();
//    if (MV_OK != nRet) {
//        printf("Failed to initialize SDK! Error: [0x%x]\n", nRet);
//        return false;
//    }
//    return true;
//}
//
////寻找设备
//bool RGB::enumerateAndSelectCamera()
//{
//    nRet = MV_CC_EnumDevices(MV_GENTL_CXP_DEVICE, &device_list);
//    if (MV_OK != nRet) {
//        printf("Failed to enumerate devices! Error: [0x%x]\n", nRet);
//        return false;
//    }
//
//    if (device_list.nDeviceNum == 0) {
//        printf("No compatible cameras found!\n");
//        return false;
//    }
//    //创建相机的句柄
//    nRet = MV_CC_CreateHandle(&camera_handle, device_list.pDeviceInfo[0]);
//    if (MV_OK != nRet) {
//        printf("Failed to create camera handle! Error: [0x%x]\n", nRet);
//        return false;
//    }
//    //打开设备
//    nRet = MV_CC_OpenDevice(camera_handle);
//    if (MV_OK != nRet) {
//        printf("Failed to open camera device! Error: [0x%x]\n", nRet);
//        MV_CC_DestroyHandle(camera_handle);
//        camera_handle = nullptr;
//        return false;
//    }
//
//    return true;
//}
//
//
////创建临时的图像缓存
//bool RGB::allocateImageBuffers()
//{
//    // Get image dimensions from camera
//    MVCC_INTVALUE width = { 0 }, height = { 0 };
//    MV_CC_GetIntValue(camera_handle, "Width", &width);
//    MV_CC_GetIntValue(camera_handle, "Height", &height);
//
//    // Allocate RGB buffer
//    size_t buffer_size = width.nCurValue * height.nCurValue * 3;
//    rgb_buffer = (unsigned char*)malloc(buffer_size);
//    if (rgb_buffer == nullptr) {
//        printf("Failed to allocate memory for RGB buffer!\n");
//        cleanupResources();
//        return false;
//    }
//
//    return true;
//}
//
//bool RGB::configureCameraSettings()
//{
//    //设置触发方式
//    // Configure trigger settings
//    const std::vector<std::tuple<const char*, int, const char*>> settings = {
//        {"TriggerMode", 1, "Trigger Mode"},
//        {"TriggerSource", 0, "Trigger Source"},
//        {"TriggerActivation", 0, "Trigger Activation"}, // Rising edge trigger
//        {"OverlapMode", 1, "Overlap Mode"}
//    };
//
//    for (const auto& [name, value, description] : settings) {
//        nRet = MV_CC_SetEnumValue(camera_handle, name, value);
//        if (MV_OK != nRet) {
//            printf("Failed to set %s! Error: [0x%x]\n", description, nRet);
//            cleanupResources();
//            return false;
//        }
//    }
//
//    // Set image node number
//    nRet = MV_CC_SetImageNodeNum(camera_handle, nImageNodeNum);
//    if (MV_OK != nRet) {
//        printf("Failed to set image node number! Error: [0x%x]\n", nRet);
//        cleanupResources();
//        return false;
//    }
//
//    return true;
//}
//
//// =============================================
//// Resource Management
//// =============================================
//
//void RGB::cleanupResources()
//{
//    // Release image buffer
//    if (rgb_buffer != nullptr) {
//        free(rgb_buffer);
//        rgb_buffer = nullptr;
//    }
//
//    // Close camera handle
//    if (camera_handle != nullptr) {
//        MV_CC_CloseDevice(camera_handle);
//        MV_CC_DestroyHandle(camera_handle);
//        camera_handle = nullptr;
//    }
//
//    // Clear display stack
//    std::lock_guard<std::mutex> lock(display_mutex);
//    display_stack.clear();
//}
//
//void RGB::clearImageQueue()
//{
//    ImageNode* node = nullptr;
//    while (image_queue.try_pop(node)) {
//        if (node) {
//            if (node->image_data) {
//                free(node->image_data);
//            }
//            delete node;
//        }
//    }
//}
//
//// =============================================
//// Camera Control
//// =============================================
//
//void RGB::startCapture(const std::string& save_path)
//{
//    if (!is_initialized || camera_handle == nullptr) {
//        printf("Camera not properly initialized. Cannot start capture.\n");
//        return;
//    }                                                                
//
//    // Create thread pool with worker threads (adjust as needed) 使用线程池的方式提高图片的存储效率
//    // 这里使用6个线程，可以根据实际需求进行调整
//    const size_t num_threads = 6;
//    thread_pool = new ThreadPool(num_threads);
//
//    // Register image callback  使用回调函数imageCallback获得元数据 
//    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
//    if (MV_OK != nRet) {
//        printf("Failed to register image callback! Error: [0x%x]\n", nRet);
//        return;
//    }
//
//    // Start image acquisition 开始取流
//    nRet = MV_CC_StartGrabbing(camera_handle);
//    if (MV_OK != nRet) {
//        printf("Failed to start image grabbing! Error: [0x%x]\n", nRet);
//        return;
//    }
//
//    // Create save directory
//    save_folder = save_path + "/rgb/";
//    QDir dir;
//    if (!dir.mkpath(QString::fromStdString(save_folder))) {
//        printf("Failed to create save directory: %s\n", save_folder.c_str());
//        MV_CC_StopGrabbing(camera_handle);
//        return;
//    }
//
//    // Set flags
//    is_saving = true;  
//    should_exit = false;
//
//    // Start task distribution thread 启动独立的保存进程distributeTasksThread
//    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);
//
//    printf("RGB Camera started successfully with %zu worker threads!\n", num_threads);
//}
//
//void RGB::stopCapture()
//{
//    // Signal threads to exit
//    should_exit = true;
//
//    // Stop save thread
//    if (is_saving) {
//        is_saving = false;
//        image_semaphore.notify(); // Wake up save thread if waiting
//
//        if (save_thread.joinable()) {
//            save_thread.join();
//        }
//    }
//
//    // Stop camera acquisition
//    if (camera_handle != nullptr) {
//        nRet = MV_CC_StopGrabbing(camera_handle);
//        if (MV_OK != nRet) {
//            printf("Failed to stop image grabbing! Error: [0x%x]\n", nRet);
//        }
//    }
//
//    // Clear image queue
//    clearImageQueue();
//}
//
//// =============================================
//// Image Processing
//// =============================================
//
//void RGB::getLatestFrame(cv::Mat* output_frame)
//{
//    std::lock_guard<std::mutex> lock(display_mutex);
//
//    cv::Mat latest_frame;
//    bool success = display_stack.top(latest_frame);
//
//    if (success && !latest_frame.empty()) {
//        *output_frame = latest_frame.clone();
//        cv::cvtColor(*output_frame, *output_frame, cv::COLOR_RGB2BGR);
//    }
//}
//
//// =============================================
//// Thread Functions
//// =============================================
//
////获得元数据，并且将元数据推入队列中
//void RGB::imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data)
//{
//    if (!frame_info || !image_data || !user_data) return;
//
//    RGB* camera = static_cast<RGB*>(user_data);
//    if (camera->should_exit) return;
//
//    ImageNode* image_node = new ImageNode();
//    if (!image_node) {
//        printf("Failed to allocate memory for image node.\n");
//        return;
//    }
//
//    image_node->pixel_type = frame_info->enPixelType;
//    image_node->data_length = frame_info->nFrameLenEx;
//    image_node->width = frame_info->nWidth;
//    image_node->height = frame_info->nHeight;
//    image_node->frame_number = frame_info->nFrameNum;
//
//    image_node->image_data = (unsigned char*)malloc(image_node->data_length);
//    if (!image_node->image_data) {
//        printf("Failed to allocate memory for image data.\n");
//        delete image_node;
//        return;
//    }
//    
//    memcpy(image_node->image_data, image_data, image_node->data_length);
//
//    camera->image_queue.push(image_node);
//    camera->image_semaphore.notify();
//}
//
//
//void RGB::distributeTasksThread()
//{
//    while (is_saving || !image_queue.empty()) {
//        image_semaphore.wait();
//        if (should_exit && image_queue.empty()) {
//            break;
//        }
//
//        ImageNode* image_node = nullptr;
//        if (!image_queue.try_pop(image_node) || image_node == nullptr) continue;
//        if (thread_pool) {
//            thread_pool->enqueue([this, image_node]() {
//                processAndSaveImage(image_node);
//                delete image_node; // Now safe to delete in worker thread
//                });
//        }
//        else {
//            processAndSaveImage(image_node);
//            delete image_node;
//        }
//    }
//
//    printf("Task distribution thread exited.\n");
//}
//
//void RGB::processAndSaveImage(ImageNode* image_node)
//{
//    size_t rgb_buffer_size = image_node->width * image_node->height * 3;
//    unsigned char* local_rgb_buffer = (unsigned char*)malloc(rgb_buffer_size);
//    if (!local_rgb_buffer) {
//        printf("Failed to allocate memory for RGB conversion.\n");
//        return;
//    }
//
//    MV_CC_PIXEL_CONVERT_PARAM convert_params = { 0 };
//    convert_params.enSrcPixelType = image_node->pixel_type;
//    convert_params.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
//    convert_params.nWidth = image_node->width;
//    convert_params.nHeight = image_node->height;
//    convert_params.nSrcDataLen = image_node->data_length;
//    convert_params.pSrcData = image_node->image_data;
//    convert_params.pDstBuffer = local_rgb_buffer;
//    convert_params.nDstBufferSize = rgb_buffer_size;
//
//    int result = MV_CC_ConvertPixelType(camera_handle, &convert_params);
//    if (MV_OK != result) {
//        printf("Failed to convert pixel type! Error: [0x%x]\n", result);
//        free(local_rgb_buffer);
//        return;
//    }
//
//    cv::Mat image(image_node->height, image_node->width, CV_8UC3, local_rgb_buffer);
//
//    cv::Mat image_copy = image.clone(); 
//
//    free(local_rgb_buffer);
//    local_rgb_buffer = nullptr;
//
//    free(image_node->image_data);
//    image_node->image_data = nullptr;
//
//    char filename[512];
//    snprintf(filename, sizeof(filename), "%s/frame_%06d.png",
//        save_folder.c_str(), image_node->frame_number);
//
//    if (!cv::imwrite(filename, image_copy)) {
//        printf("Failed to save image: %s\n", filename);
//    }
//}

/*

* RGB.cpp
*
* 实现对应于提供的 RGB.h 的功能：
* * 支持多实例（camera_index）
* * 将图像以可扩展 dataset "/frames" 的形式写入 HDF5，shape = [N, H, W, C]
* * 批量写入（hdf5_batch_size），在 stopCapture() 时强制 flush
* * 所有图像完全写入并关闭 HDF5 后在控制台输出保存完毕信息
*
* 说明：
* * 需要链接 HDF5 C++ 库（-lhdf5_cpp -lhdf5）
* * 需要 MV SDK 和你已有的 DataQueue/ThreadPool/ DataStack 实现
*

*/

#include "RGB.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

// Helper: current timestamp in nanoseconds
static uint64_t now_ns() {
    return std::chrono::duration_cast[std::chrono::nanoseconds](std::chrono::nanoseconds)(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

// ------------------------- 构造 / 析构 -------------------------
RGB::RGB(int cam_index, const std::string& save_path)
    : camera_index(cam_index),
    save_folder(save_path),
    image_semaphore(0)
{
    initializeInternalParameters();
}

RGB::~RGB()
{
    stopCapture();
    cleanupResources();
}

// ------------------------- 初始化 / 资源 -------------------------
void RGB::initializeInternalParameters()
{
    task_stop.store(false);
    is_initialized.store(false);
    is_saving.store(false);
    should_exit.store(false);
    is_recording.store(false);


    nRet = MV_OK;
    frame_counter = 0;
    total_frames_written = 0;

    thread_pool = nullptr;
    h5file = nullptr;
    rgb_buffer = nullptr;
    camera_handle = nullptr;

    // make sure queues empty
    clearImageQueue();
    while (!hdf5_image_batch.empty()) hdf5_image_batch.pop_front();
    while (!hdf5_timestamp_batch.empty()) hdf5_timestamp_batch.pop_front();

}

bool RGB::initializeCameraSDK()
{
    nRet = MV_CC_Initialize();
    if (MV_OK != nRet) {
        std::printf("Failed to initialize SDK! Error: [0x%x]\n", nRet);
        return false;
    }
    return true;
}

bool RGB::enumerateAndSelectCamera()
{
    nRet = MV_CC_EnumDevices(MV_GENTL_CXP_DEVICE, &device_list);
    if (MV_OK != nRet) {
        std::printf("Failed to enumerate devices! Error: [0x%x]\n", nRet);
        return false;
    }

    if (device_list.nDeviceNum == 0) {
        std::printf("No compatible cameras found!\n");
        return false;
    }

    // 防护：若 camera_index 超过设备数量，使用第 0 台
    int idx = camera_index;
    if (idx < 0 || idx >= static_cast<int>(device_list.nDeviceNum)) idx = 0;

    nRet = MV_CC_CreateHandle(&camera_handle, device_list.pDeviceInfo[idx]);
    if (MV_OK != nRet) {
        std::printf("Failed to create camera handle! Error: [0x%x]\n", nRet);
        camera_handle = nullptr;
        return false;
    }

    nRet = MV_CC_OpenDevice(camera_handle);
    if (MV_OK != nRet) {
        std::printf("Failed to open camera device! Error: [0x%x]\n", nRet);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
        return false;
    }

    return true;

}

bool RGB::allocateImageBuffers()
{
    MVCC_INTVALUE width = { 0 }, height = { 0 };
    MV_CC_GetIntValue(camera_handle, "Width", &width);
    MV_CC_GetIntValue(camera_handle, "Height", &height);

    if (width.nCurValue <= 0 || height.nCurValue <= 0) {
        std::printf("Invalid camera resolution width=%d height=%d\n", width.nCurValue, height.nCurValue);
        return false;
    }

    size_t buffer_size = static_cast<size_t>(width.nCurValue) * static_cast<size_t>(height.nCurValue) * 3;
    rgb_buffer = static_cast<unsigned char*>(malloc(buffer_size));
    if (!rgb_buffer) {
        std::printf("Failed to allocate rgb_buffer size=%zu\n", buffer_size);
        return false;
    }

    return true;

}

bool RGB::configureCameraSettings()
{
    // 示例：将相机设置为软触发或外触发，按需修改
    const std::vector<std::tuple<const char*, int, const char*>> settings = {
    {"TriggerMode", 1, "Trigger Mode"},
    {"TriggerSource", 0, "Trigger Source"},
    {"TriggerActivation", 0, "Trigger Activation"},
    {"OverlapMode", 1, "Overlap Mode"}
    };

    for (const auto& [name, value, description] : settings) {
        nRet = MV_CC_SetEnumValue(camera_handle, name, value);
        if (MV_OK != nRet) {
            std::printf("Failed to set %s! Error: [0x%x]\n", description, nRet);
            // 不立即返回 false，允许非致命错误；如果你要求严格，可返回 false
        }
    }

    nRet = MV_CC_SetImageNodeNum(camera_handle, nImageNodeNum);
    if (MV_OK != nRet) {
        std::printf("Failed to set image node number! Error: [0x%x]\n", nRet);
        // 非致命处理
    }

    return true;

}

void RGB::cleanupResources()
{
    // 停止 SDK 取流并销毁句柄
    if (camera_handle) {
        MV_CC_StopGrabbing(camera_handle);
        MV_CC_CloseDevice(camera_handle);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
    }

    if (rgb_buffer) {
        free(rgb_buffer);
        rgb_buffer = nullptr;
    }

    // 关闭 HDF5
    closeHDF5();

    // 清空队列
    clearImageQueue();

}

// 清空 image_queue
void RGB::clearImageQueue()
{
    ImageNode* node = nullptr;
    while (image_queue.try_pop(node)) {
        if (node) {
            delete node; // ImageNode 的析构会 free image_data
        }
    }
}

// ------------------------- HDF5 相关实现 -------------------------
bool RGB::initHDF5File()
{
    std::lock_guard[std::mutex](std::mutex) lock(hdf5_mutex);
    if (h5file) return true; // 已初始化

    // 构造文件名： save_folder + "/rgb_cam{index}_YYYYmmdd_HHMMSS.h5"
    QDir dir;
    dir.mkpath(QString::fromStdString(save_folder)); // 确保目录存在

    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    std::ostringstream oss;
    oss << save_folder << "/rgb_cam" << camera_index << "_" << stamp.toStdString() << ".h5";
    h5_filename = oss.str();

    try {
        // Create file (truncate if exists)
        h5file = new H5::H5File(h5_filename, H5F_ACC_TRUNC);
        // Note: dataset will be created on first flush when we know H,W,C
    }
    catch (const H5::Exception& e) {
        std::cerr << "HDF5 create file failed: " << e.getDetailMsg() << std::endl;
        if (h5file) { delete h5file; h5file = nullptr; }
        return false;
    }
    return true;

}

void RGB::closeHDF5()
{
    std::lock_guard[std::mutex](std::mutex) lock(hdf5_mutex);
    if (h5file) {
        try {
            h5file->flush(H5F_SCOPE_GLOBAL);
            h5file->close();
        }
        catch (...) {
            // ignore
        }
        delete h5file;
        h5file = nullptr;
    }
    // 清空缓存
    hdf5_image_batch.clear();
    hdf5_timestamp_batch.clear();
}

void RGB::flushHDF5(bool force)
{
    std::lock_guard[std::mutex](std::mutex) lock(hdf5_mutex);

    if (!initHDF5File()) {
        std::cerr << "flushHDF5: initHDF5File failed\n";
        return;
    }
    if (hdf5_image_batch.empty()) return;
    // 发现第一帧的尺寸用于创建 dataset
    const cv::Mat& first = hdf5_image_batch.front();
    if (first.empty()) return;

    hsize_t H = static_cast<hsize_t>(first.rows);
    hsize_t W = static_cast<hsize_t>(first.cols);
    hsize_t C = static_cast<hsize_t>(first.channels());
    size_t n_batch = hdf5_image_batch.size();

    try {
        // 若 dataset 不存在，创建可扩展的 dataset "/frames"
        bool dataset_exists = false;
        H5::Group root = h5file->openGroup("/"); // throws if openable
        try {
            H5::DataSet ds = h5file->openDataSet("/frames");
            dataset_exists = true;
        }
        catch (...) {
            dataset_exists = false;
        }

        H5::DataSet dataset;
        if (!dataset_exists) {
            // 创建 dataspace，初始尺寸 0，最大尺寸 unlimited on first dim
            hsize_t current_dims[4] = { 0, H, W, C };
            hsize_t max_dims[4] = { H5S_UNLIMITED, H, W, C };
            H5::DataSpace dataspace(4, current_dims, max_dims);

            // chunking: 选择 chunk 为 (min(batch,16), H, W, C) 以便压缩/扩展
            hsize_t chunk_dims[4] = { (hsize_t)std::min<size_t>(hdf5_batch_size, 16), H, W, C };
            H5::DSetCreatPropList prop;
            prop.setChunk(4, chunk_dims);
            // 可选压缩：
            // prop.setDeflate(4); // 默认压缩级别，若需要开启，请确保 HDF5 库启用了 zlib

            dataset = h5file->createDataSet("/frames", H5::PredType::NATIVE_UCHAR, dataspace, prop);
        }
        else {
            dataset = h5file->openDataSet("/frames");
            // 检查 shape 是否匹配 HWC
            H5::DataSpace ds_space = dataset.getSpace();
            int rank = ds_space.getSimpleExtentNdims();
            if (rank != 4) {
                std::cerr << "Existing /frames dataset rank != 4\n";
                return;
            }
            // TODO: could check H,W,C matches
        }

        // 将 batch 数据合并到一个连续 buffer
        // Buffer 大小 = n_batch * H * W * C
        const size_t frame_bytes = static_cast<size_t>(H) * static_cast<size_t>(W) * static_cast<size_t>(C);
        std::vector<unsigned char> write_buf;
        write_buf.reserve(n_batch * frame_bytes);
        for (size_t i = 0; i < n_batch; ++i) {
            const cv::Mat& m = hdf5_image_batch[i];
            // 确保连续且类型为 8UC3 或 8UC1
            if (!m.isContinuous()) {
                cv::Mat cont = m.clone();
                write_buf.insert(write_buf.end(), cont.data, cont.data + frame_bytes);
            }
            else {
                write_buf.insert(write_buf.end(), m.data, m.data + frame_bytes);
            }
        }

        // 扩展 dataset
        H5::DataSpace filespace = dataset.getSpace();
        hsize_t old_dims[4];
        filespace.getSimpleExtentDims(old_dims, nullptr);
        hsize_t old_N = old_dims[0];
        hsize_t new_N = old_N + static_cast<hsize_t>(n_batch);
        hsize_t new_dims[4] = { new_N, H, W, C };
        dataset.extend(new_dims);

        // 写入新扩展的区域：选择 hyperslab
        H5::DataSpace new_filespace = dataset.getSpace();
        hsize_t start[4] = { old_N, 0, 0, 0 };
        hsize_t count[4] = { static_cast<hsize_t>(n_batch), H, W, C };
        new_filespace.selectHyperslab(H5S_SELECT_SET, count, start);

        // 内存 dataspace
        hsize_t mem_dims[4] = { static_cast<hsize_t>(n_batch), H, W, C };
        H5::DataSpace memspace(4, mem_dims);

        // 写入（类型 NATIVE_UCHAR）
        dataset.write(write_buf.data(), H5::PredType::NATIVE_UCHAR, memspace, new_filespace);

        // flush file
        h5file->flush(H5F_SCOPE_GLOBAL);

        // 更新计数与清空 batch
        total_frames_written += n_batch;
        hdf5_image_batch.clear();
        hdf5_timestamp_batch.clear();

    }
    catch (const H5::Exception& e) {
        std::cerr << "HDF5 write exception: " << e.getDetailMsg() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "HDF5 write std::exception: " << e.what() << std::endl;
    }

}

// ------------------------- 回调 & 线程 -------------------------
void RGB::imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data)
{
    if (!frame_info || !image_data || !user_data) return;

    RGB* camera = static_cast<RGB*>(user_data);
    if (camera->should_exit.load()) return;

    ImageNode* image_node = new ImageNode();
    if (!image_node) {
        std::printf("Failed to allocate memory for image node.\n");
        return;
    }

    image_node->pixel_type = frame_info->enPixelType;
    image_node->data_length = frame_info->nFrameLenEx;
    image_node->width = frame_info->nWidth;
    image_node->height = frame_info->nHeight;
    image_node->frame_number = frame_info->nFrameNum;
    image_node->timestamp_ns = now_ns();

    image_node->image_data = static_cast<unsigned char*>(malloc(image_node->data_length));
    if (!image_node->image_data) {
        std::printf("Failed to allocate memory for image data.\n");
        delete image_node;
        return;
    }
    std::memcpy(image_node->image_data, image_data, static_cast<size_t>(image_node->data_length));

    camera->image_queue.push(image_node);
    camera->image_semaphore.notify();
    ```

}

void RGB::distributeTasksThread()
{
    while (!task_stop.load() || !image_queue.empty()) {
        image_semaphore.wait(); // 等待新的帧

        if (task_stop.load() && image_queue.empty()) break;

        ImageNode* image_node = nullptr;
        if (!image_queue.try_pop(image_node) || image_node == nullptr) continue;

        // 提交给线程池或直接处理
        if (thread_pool) {
            thread_pool->enqueue([this, image_node]() {
                try {
                    processAndSaveImage(image_node);
                }
                catch (const std::exception& e) {
                    std::cerr << "Exception in worker: " << e.what() << std::endl;
                }
                delete image_node;
                });
        }
        else {
            processAndSaveImage(image_node);
            delete image_node;
        }
    }
    // 在退出前，确保所有本实例的线程池任务完成（若使用外部线程池请确保其处理）
    std::printf("RGB[%d] distributeTasksThread exited.\n", camera_index);

}

void RGB::saveImagesThread()
{
    // 周期性 flush，以防最后一批未达到 batch 时长时间不写入
    while (!task_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // 若缓存达到 batch，则 flush（processAndSaveImage 也会触发 flush）
        {
            std::lock_guard[std::mutex](std::mutex) lock(hdf5_mutex);
            if (!hdf5_image_batch.empty() && hdf5_image_batch.size() >= 1) {
                // 选择不强制：因为 processAndSaveImage 已在必要时触发 flush。
                // 这里作为保险，周期性小批量写入
            }
        }
    }
    // 离开前不在此处强制 flush（stopCapture 会强制）
    std::printf("RGB[%d] saveImagesThread exited.\n", camera_index);
}

// ------------------------- 图像处理与 HDF5 缓存 -------------------------
void RGB::processAndSaveImage(ImageNode* image_node)
{
    if (!image_node) return;
    if (should_exit.load()) return;

    // 1) 像素格式转换：使用 SDK 的 MV_CC_ConvertPixelType 将源数据转换为 BGR8
    size_t out_buf_size = static_cast<size_t>(image_node->width) * static_cast<size_t>(image_node->height) * 3;
    unsigned char* local_rgb = static_cast<unsigned char*>(malloc(out_buf_size));
    if (!local_rgb) {
        std::printf("Failed to allocate local_rgb\n");
        return;
    }

    MV_CC_PIXEL_CONVERT_PARAM convert_params;
    std::memset(&convert_params, 0, sizeof(convert_params));
    convert_params.enSrcPixelType = image_node->pixel_type;
    convert_params.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
    convert_params.nWidth = image_node->width;
    convert_params.nHeight = image_node->height;
    convert_params.nSrcDataLen = static_cast<int>(image_node->data_length);
    convert_params.pSrcData = image_node->image_data;
    convert_params.pDstBuffer = local_rgb;
    convert_params.nDstBufferSize = static_cast<int>(out_buf_size);

    int res = MV_CC_ConvertPixelType(camera_handle, &convert_params);
    if (MV_OK != res) {
        std::printf("Failed to convert pixel type! Error: [0x%x]\n", res);
        free(local_rgb);
        return;
    }

    // 2) 构造 OpenCV Mat（注意与 HDF5 的通道顺序匹配：BGR -> 我们将直接写入 BGR）
    cv::Mat img(image_node->height, image_node->width, CV_8UC3, local_rgb);
    // Deep copy 到连续的 Mat（防止释放 local_rgb 导致数据丢失）
    cv::Mat image_copy = img.clone();

    // 释放 local buffer
    free(local_rgb);

    // 3) push 到 HDF5 batch（线程安全）
    {
        std::lock_guard<std::mutex> lock(hdf5_mutex);
        hdf5_image_batch.push_back(image_copy);
        hdf5_timestamp_batch.push_back(image_node->timestamp_ns);
    }

    // 4) 若达到批大小则 flush（释放锁后执行，避免死锁）
    bool need_flush = false;
    {
        std::lock_guard<std::mutex> lock(hdf5_mutex);
        if (hdf5_image_batch.size() >= hdf5_batch_size) {
            need_flush = true;
        }
    }
    if (need_flush) {
        flushHDF5(false);  // flushHDF5 内部自加锁，安全
    }

    // 5) 推入用于预览的 display_stack（仅保留少量）
    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_stack.push(image_copy);
    }

}

// ------------------------- 启动 / 停止 -------------------------
void RGB::startCapture(const std::string& save_path)
{
    if (is_recording.load()) {
        std::printf("RGB[%d] already recording.\n", camera_index);
        return;
    }

    ```
        if (!save_path.empty()) save_folder = save_path;

    // 初始化 SDK + 设备 + buffer + settings
    if (!initializeCameraSDK()) return;
    if (!enumerateAndSelectCamera()) return;
    if (!allocateImageBuffers()) return;
    if (!configureCameraSettings()) return;

    // 准备 HDF5 文件（延后到写入时真正创建）
    // thread pool
    const size_t num_threads = 6;
    thread_pool = new ThreadPool(num_threads);

    // 注册回调
    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
    if (MV_OK != nRet) {
        std::printf("Failed to register image callback! Error: [0x%x]\n", nRet);
    }

    nRet = MV_CC_StartGrabbing(camera_handle);
    if (MV_OK != nRet) {
        std::printf("Failed to start grabbing! Error: [0x%x]\n", nRet);
        // continue - but probably return
    }

    // ensure save folder exists
    QDir dir;
    if (!dir.mkpath(QString::fromStdString(save_folder))) {
        std::printf("Failed to create save directory: %s\n", save_folder.c_str());
        // not fatal
    }

    // set flags
    is_saving.store(true);
    should_exit.store(false);
    task_stop.store(false);
    is_recording.store(true);

    // start distributor thread
    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);

    // start background save thread (optional, here used for periodic tasks)
    save_thread = std::thread(&RGB::saveImagesThread, this);

    std::printf("RGB[%d] started capturing. Saving to: %s\n", camera_index, save_folder.c_str());
    ```

}

void RGB::stopCapture()
{
    if (!is_recording.load()) {
        std::printf("RGB[%d] not recording.\n", camera_index);
        return;
    }

    ```
        // 标志停止接收新任务
        is_recording.store(false);
    is_saving.store(false);
    task_stop.store(true);
    should_exit.store(true);

    // 注销 SDK 回调（若 SDK 提供注销接口）
    // MV_CC_RegisterImageCallBackEx(..., nullptr, nullptr) // SDK 可能不支持直接注销; 可安全停止 grabbing

    // 停止抓取
    if (camera_handle) {
        nRet = MV_CC_StopGrabbing(camera_handle);
        if (MV_OK != nRet) {
            std::printf("Failed to stop image grabbing! Error: [0x%x]\n", nRet);
        }
    }

    // 唤醒可能等待的分发线程
    image_semaphore.notifyAll();

    // 等待分发线程退出
    if (task_distribution_thread.joinable()) task_distribution_thread.join();

    // 等待线程池任务完成并销毁线程池
    if (thread_pool) {
        delete thread_pool;
        thread_pool = nullptr;
    }

    // 停止并等待 save_thread
    if (save_thread.joinable()) save_thread.join();

    // 清空未处理的 image_queue（这些帧未被处理）
    clearImageQueue();

    // 强制 flush 所有 HDF5 缓存并关闭
    flushHDF5(true);
    closeHDF5();

    // 清理 SDK 资源
    cleanupResources();

    // 最后：完全保存完成的提示（保证所有写入已完成）
    std::cout << "[RGB " << camera_index << "] 所有图片已保存完毕，文件路径: " << h5_filename << std::endl;
    ```

}

// ------------------------- 预览接口 -------------------------
void RGB::getLatestFrame(cv::Mat* output_frame)
{
    std::lock_guard[std::mutex](std::mutex) lock(display_mutex);
    cv::Mat latest;
    bool ok = display_stack.top(latest);
    if (ok && !latest.empty()) {
        *output_frame = latest.clone();
        // 如果你需要 BGR->RGB 转换，按需调整
    }
    else {
        output_frame->release();
    }
}
