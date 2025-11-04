#include "RGB.h"
#include <H5Cpp.h> // 引入 HDF5 C++ API
#include <memory>  // 引入 std::make_unique

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
    task_stop = false; // (from your .h)

    // Clear data structures
    image_queue.clear();
    hdf5_write_queue.clear(); // +++ ADDED: 清空新队列

    // Initialize camera parameters
    nRet = MV_OK;
    camera_handle = nullptr;
    nImageNodeNum = 200; // (from your .h, was 200 in old .cpp)

    // Initialize buffers
    rgb_buffer = nullptr;

    // Initialize camera SDK structures
    memset(&device_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    memset(&pixel_convert_params, 0, sizeof(MV_CC_PIXEL_CONVERT_PARAM));
    memset(&output_frame, 0, sizeof(MV_FRAME_OUT));
    memset(&image_save_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM));
    // memset(&image_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM)); // (in old .cpp, not in .h)
    memset(&int_value_params, 0, sizeof(MVCC_INTVALUE));

    thread_pool = nullptr;

    // +++ ADDED: Initialize HDF5 members
    h5_file.reset(); // (h5_file is std::unique_ptr)
    h5_rgb_dataset = H5::DataSet();   // 重置为无效句柄
}

// 构造函数
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

// 析构函数
RGB::~RGB()
{
    // 确保在析构时停止所有活动
    if (is_saving) {
        stopCapture();
    }
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
            // ImageNode 的析构函数会自动 free(node->image_data)
            delete node;
        }
    }
}

// +++ ADDED: 清理新的HDF5队列
void RGB::clearHDF5Queue()
{
    // <<< FIX: 变量名和类型修正
    ProcessedFrame* frame_ptr = nullptr;
    while (hdf5_write_queue.try_pop(frame_ptr)) {
        if (frame_ptr) {
            // ProcessedFrame 包含一个 cv::Mat，它会自动释放
            delete frame_ptr; // <<< FIX: 删除指针
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

    // +++ ADDED: 初始化 HDF5 文件
    if (!initializeHDF5(save_path)) {
        printf("Failed to initialize HDF5 file. Cannot start capture.\n");
        return;
    }

    // Create thread pool
    const size_t num_threads = 6;
    thread_pool = new ThreadPool(num_threads);

    // Register image callback
    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
    if (MV_OK != nRet) {
        printf("Failed to register image callback! Error: [0x%x]\n", nRet);
        closeHDF5();
        return;
    }

    // Start image acquisition
    nRet = MV_CC_StartGrabbing(camera_handle);
    if (MV_OK != nRet) {
        printf("Failed to start image grabbing! Error: [0x%x]\n", nRet);
        closeHDF5();
        return;
    }

    // Set flags
    is_saving = true;
    should_exit = false;

    // Start task distribution thread
    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);

    // +++ ADDED: 启动专用的 HDF5 写入线程
    hdf5_writer_thread = std::thread(&RGB::hdf5WriteLoop, this);

    printf("RGB Camera started successfully with %zu worker threads (HDF5 mode)!\n", num_threads);
}

void RGB::stopCapture()
{
    // 1. Signal all threads to exit
    should_exit = true; // 信号采集回调 (imageCallback)
    is_saving = false;  // 信号分发 (distribute) 和写入 (hdf5WriteLoop) 线程

    // 2. Wake up threads that might be waiting
    image_semaphore.notifyAll(); // (from your .h) 唤醒 distributeTasksThread
    //hdf5_write_queue.stopWait(); // +++ ADDED: 唤醒 hdf5_writer_thread (假设 DataQueue 有此方法)
    // 如果 DataQueue 没有，hdf5_writer_thread 中的 is_saving 检查会处理

// 3. Join threads (order matters: consumers first)

// +++ CHANGED: 首先停止并等待 HDF5 线程 (最后的消费者)
    if (hdf5_writer_thread.joinable()) {
        hdf5_writer_thread.join();
        printf("HDF5 writer thread joined.\n");
    }

    // +++ CHANGED: 然后停止并等待分发线程 (中间人)
    if (task_distribution_thread.joinable()) {
        task_distribution_thread.join();
        printf("Task distributor thread joined.\n");
    }


    // 4. Stop camera hardware
    if (camera_handle != nullptr) {
        MV_CC_StopGrabbing(camera_handle);
        MV_CC_RegisterImageCallBackEx(camera_handle, NULL, NULL);
    }

    // 5. Close HDF5 file
    closeHDF5();
    printf("HDF5 file closed.\n");

    // 6. Clean up thread pool
    if (thread_pool) {
        delete thread_pool; // ThreadPool 析构函数应等待所有任务完成
        thread_pool = nullptr;
    }

    // 7. Clear remaining data in queues
    clearImageQueue();
    clearHDF5Queue();
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
        // <<< REMOVED: cv::cvtColor(*output_frame, *output_frame, cv::COLOR_RGB2BGR);
        // 我们在 processAndQueueFrame 中推入的已经是 BGR 格式
    }
}

// =============================================
// Thread Functions
// =============================================

// 获得元数据，并且将元数据推入队列中
void RGB::imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data)
{
    if (!frame_info || !image_data || !user_data) return;

    RGB* camera = static_cast<RGB*>(user_data);
    if (camera->should_exit) return; // 快速退出

    // Create new image node
    ImageNode* image_node = new ImageNode();
    if (!image_node) {
        printf("Failed to allocate memory for image node.\n");
        return;
    }

    // Populate image node
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
    camera->image_queue.push(image_node);
    camera->image_semaphore.notify();
}

// 分发任务线程 (从 L1 队列 -> 线程池)
void RGB::distributeTasksThread()
{
    while (is_saving || !image_queue.empty()) {
        image_semaphore.wait(); // 等待 imageCallback 的信号

        if (should_exit && image_queue.empty()) {
            break;
        }

        ImageNode* image_node = nullptr;
        if (!image_queue.try_pop(image_node) || image_node == nullptr) {
            if (!is_saving) break; // 队列空了，且停止保存了，就退出
            continue;
        }

        // 提交到线程池
        if (thread_pool) {
            thread_pool->enqueue([this, image_node]() {
                // <<< CHANGED: 调用新函数
                processAndQueueFrame(image_node);
                delete image_node; // 在工作线程中安全删除
                });
        }
        else {
            // Fallback
            processAndQueueFrame(image_node);
            delete image_node;
        }
    }
    printf("Task distribution thread exited.\n");
}

// <<< REPLACED: 线程池的工作 (格式转换 + 推入HDF5队列)
void RGB::processAndQueueFrame(ImageNode* image_node)
{
    // Allocate RGB buffer for conversion
    size_t rgb_buffer_size = image_node->width * image_node->height * 3;
    unsigned char* local_rgb_buffer = (unsigned char*)malloc(rgb_buffer_size);
    if (!local_rgb_buffer) {
        printf("Failed to allocate memory for RGB conversion.\n");
        free(image_node->image_data); // 别忘了释放
        image_node->image_data = nullptr;
        return;
    }

    // Convert pixel format
    MV_CC_PIXEL_CONVERT_PARAM convert_params = { 0 };
    convert_params.enSrcPixelType = image_node->pixel_type;
    convert_params.enDstPixelType = PixelType_Gvsp_BGR8_Packed; // 转为 BGR
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
        free(image_node->image_data);
        image_node->image_data = nullptr;
        return;
    }

    // Create OpenCV image (wrapper, no copy)
    cv::Mat image_wrapper(image_node->height, image_node->width, CV_8UC3, local_rgb_buffer);

    // --- 核心修改 ---

    // 1. 创建新的 ProcessedFrame
    ProcessedFrame* p_frame = new ProcessedFrame();
    p_frame->frame = image_wrapper.clone(); // 深拷贝！
    p_frame->frame_number = image_node->frame_number;

    // 2. 推送到 HDF5 写入队列
    hdf5_write_queue.push(p_frame);

    // 3. (FIX) 推送到 UI 显示队列 (修复UI卡死问题)
    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_stack.push(p_frame->frame); // 推送深拷贝的帧
    }

    // 4. 清理
    free(local_rgb_buffer);
    free(image_node->image_data);
    image_node->image_data = nullptr; // 防止 ImageNode 析构函数二次释放
}

// +++ ADDED: HDF5 写入线程循环
void RGB::hdf5WriteLoop()
{
    printf("HDF5 writer thread started.\n");
    while (is_saving || !hdf5_write_queue.empty())
    {
        ProcessedFrame* frame_to_save = nullptr;

        // 使用 DataQueue 的阻塞-pop (假设它有带超时的 wait_pop)
        // bool got_frame = hdf5_write_queue.wait_pop(frame_to_save, 100); // 100ms 超时

        // 或者，使用 try_pop 轮询
        if (hdf5_write_queue.try_pop(frame_to_save)) {
            if (frame_to_save) {
                try {
                    extendAndWriteHDF5(frame_to_save);
                }
                catch (H5::Exception& e) {
                    printf("HDF5 write error: %s\n", e.getCDetailMsg());
                }
                delete frame_to_save;
            }
        }
        else {
            // 队列为空
            if (!is_saving) {
                break; // 停止标志置位 且 队列已空，退出
            }
            // 队列为空，但仍在保存，休息一下
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    printf("HDF5 writer thread exiting.\n");
}

// +++ ADDED: HDF5 初始化
bool RGB::initializeHDF5(const std::string& base_path)
{
    // HDF5 错误会抛出异常，所以我们用 try-catch
    try {
        // 1. 获取图像尺寸 (在 initializeHDF5 中重新获取，更安全)
        MVCC_INTVALUE width_info = { 0 }, height_info = { 0 };
        nRet = MV_CC_GetIntValue(camera_handle, "Width", &width_info);
        if (nRet != MV_OK) return false;
        nRet = MV_CC_GetIntValue(camera_handle, "Height", &height_info);
        if (nRet != MV_OK) return false;

        uint64_t width = width_info.nCurValue;
        uint64_t height = height_info.nCurValue;
        unsigned int channels = 3;

        // 2. 创建 HDF5 文件
        std::string h5_filename = base_path + "/rgb_data.h5";
        h5_file = std::make_unique<H5::H5File>(h5_filename, H5F_ACC_TRUNC);

        // 3. 创建组 (Group)
        H5::Group rgb_group = h5_file->createGroup("/rgb");

        // 4. 创建图像数据集 (/rgb/frames)
        hsize_t rgb_dims[4] = { 0, (hsize_t)height, (hsize_t)width, (hsize_t)channels }; // 初始维度 (N, H, W, C)
        hsize_t rgb_maxdims[4] = { H5S_UNLIMITED, (hsize_t)height, (hsize_t)width, (hsize_t)channels };
        H5::DataSpace rgb_dataspace(4, rgb_dims, rgb_maxdims);

        // -- 设置分块 (Chunking) 以便扩展 --
        H5::DSetCreatPropList rgb_props;
        hsize_t chunk_dims[4] = { 1, (hsize_t)height, (hsize_t)width, (hsize_t)channels }; // 每次写入1帧
        rgb_props.setChunk(4, chunk_dims);
        // (可选: 开启压缩)
        // rgb_props.setDeflate(6); 

        h5_rgb_dataset = rgb_group.createDataSet("frames", H5::PredType::NATIVE_UINT8, rgb_dataspace, rgb_props);
        h5_rgb_dims[0] = 0; // 存储当前帧数
        h5_rgb_dims[1] = height;
        h5_rgb_dims[2] = width;
        h5_rgb_dims[3] = channels;
    }
    catch (H5::Exception& e) {
        printf("Failed to initialize HDF5: %s\n", e.getCDetailMsg());
        return false;
    }
    return true;
}

// +++ ADDED: HDF5 写入单帧
void RGB::extendAndWriteHDF5(ProcessedFrame* frame)
{
    // (此函数在 hdf5WriteLoop 单线程中调用，不需要 mutex)
    try {
        // 写入图像数据 
        h5_rgb_dims[0]++; // 帧数+1
        h5_rgb_dataset.extend(h5_rgb_dims); // 扩展数据集

        H5::DataSpace file_space = h5_rgb_dataset.getSpace();
        hsize_t offset[4] = { h5_rgb_dims[0] - 1, 0, 0, 0 }; // 计算偏移量
        hsize_t slab_dims[4] = { 1, (hsize_t)frame->frame.rows, (hsize_t)frame->frame.cols, (hsize_t)frame->frame.channels() };
        file_space.selectHyperslab(H5S_SELECT_SET, slab_dims, offset);

        H5::DataSpace mem_space(4, slab_dims, NULL);
        h5_rgb_dataset.write(frame->frame.data, H5::PredType::NATIVE_UINT8, mem_space, file_space);

    }
    catch (H5::Exception& e) {
        printf("HDF5 extend/write error: %s\n", e.getCDetailMsg());
        // (可能需要设置一个错误标志)
    }
}

// +++ ADDED: HDF5 关闭
void RGB::closeHDF5()
{
    std::lock_guard<std::mutex> lock(h5_mutex); // 保护 HDF5 句柄
    try {
        // 检查句柄是否有效，然后关闭
        if (h5_rgb_dataset.getId() >= 0) {
            h5_rgb_dataset.close();
            h5_rgb_dataset = H5::DataSet(); // 重置
        }
        if (h5_file) {
            h5_file->close();
            h5_file.reset(); // 释放 unique_ptr
        }
    }
    catch (H5::Exception& e) {
        printf("Error closing HDF5 file: %s\n", e.getCDetailMsg());
    }
}