#include "RGB.h"
#include <H5Cpp.h>
#include <memory>
#include <cstdio> // for printf

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
    task_stop = false;

    // Clear data structures
    image_queue.clear();
    hdf5_write_queue.clear();

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
    memset(&int_value_params, 0, sizeof(MVCC_INTVALUE));

    thread_pool = nullptr;

    // Initialize HDF5 members
    h5_file.reset(); 
    h5_rgb_dataset = H5::DataSet(); 
    h5_frame_num_dataset = H5::DataSet(); // [新增] 初始化
}

RGB::RGB()
{
    initializeInternalParameters();

    if (!initializeCameraSDK()) return;
    if (!enumerateAndSelectCamera()) return;
    if (!allocateImageBuffers()) return;
    if (!configureCameraSettings()) return;

    is_initialized = true;
    printf("RGB camera initialized successfully.\n");
}

RGB::~RGB()
{
    if (is_saving) {
        stopCapture();
    }
    cleanupResources();
}

// =============================================
// Camera Initialization Helpers
// =============================================

bool RGB::initializeCameraSDK()
{
    nRet = MV_CC_Initialize();
    if (MV_OK != nRet) {
        printf("Failed to initialize SDK! Error: [0x%x]\n", nRet);
        return false;
    }
    return true;
}

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
    
    nRet = MV_CC_CreateHandle(&camera_handle, device_list.pDeviceInfo[0]);
    if (MV_OK != nRet) {
        printf("Failed to create camera handle! Error: [0x%x]\n", nRet);
        return false;
    }
    
    nRet = MV_CC_OpenDevice(camera_handle);
    if (MV_OK != nRet) {
        printf("Failed to open camera device! Error: [0x%x]\n", nRet);
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
    // Configure trigger settings (Rising Edge Trigger)
    const std::vector<std::tuple<const char*, int, const char*>> settings = {
        {"TriggerMode", 1, "Trigger Mode"},
        {"TriggerSource", 0, "Trigger Source"},
        {"TriggerActivation", 0, "Trigger Activation"}, 
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
    if (rgb_buffer != nullptr) {
        free(rgb_buffer);
        rgb_buffer = nullptr;
    }

    if (camera_handle != nullptr) {
        MV_CC_CloseDevice(camera_handle);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
    }

    std::lock_guard<std::mutex> lock(display_mutex);
    display_stack.clear();
}

void RGB::clearImageQueue()
{
    ImageNode* node = nullptr;
    while (image_queue.try_pop(node)) {
        if (node) delete node;
    }
}

void RGB::clearHDF5Queue()
{
    ProcessedFrame* frame_ptr = nullptr;
    while (hdf5_write_queue.try_pop(frame_ptr)) {
        if (frame_ptr) delete frame_ptr;
    }
}

// =============================================
// Camera Control
// =============================================

void RGB::startCapture(const std::string& save_path)
{
    if (!is_initialized || camera_handle == nullptr) {
        printf("Camera not properly initialized.\n");
        return;
    }

    // Initialize HDF5 (with frame_nums)
    if (!initializeHDF5(save_path)) {
        printf("Failed to initialize HDF5 file.\n");
        return;
    }

    // Thread pool
    const size_t num_threads = 6;
    thread_pool = new ThreadPool(num_threads);

    // Register callback
    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
    if (MV_OK != nRet) {
        printf("Failed to register image callback! Error: [0x%x]\n", nRet);
        closeHDF5();
        return;
    }

    // Start grabbing
    nRet = MV_CC_StartGrabbing(camera_handle);
    if (MV_OK != nRet) {
        printf("Failed to start grabbing! Error: [0x%x]\n", nRet);
        closeHDF5();
        return;
    }

    is_saving = true;
    should_exit = false;

    // Start threads
    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);
    hdf5_writer_thread = std::thread(&RGB::hdf5WriteLoop, this);

    printf("RGB Camera started (HDF5 mode, %zu threads).\n", num_threads);
}

void RGB::stopCapture()
{
    // Signal stops
    should_exit = true;
    is_saving = false;

    // Wake up threads
    image_semaphore.notifyAll();
    
    // Join threads (Consumer first)
    if (hdf5_writer_thread.joinable()) {
        hdf5_writer_thread.join();
        printf("HDF5 writer thread joined.\n");
    }

    if (task_distribution_thread.joinable()) {
        task_distribution_thread.join();
        printf("Task distributor thread joined.\n");
    }

    // Stop hardware
    if (camera_handle != nullptr) {
        MV_CC_StopGrabbing(camera_handle);
        MV_CC_RegisterImageCallBackEx(camera_handle, NULL, NULL);
    }

    // Close HDF5
    closeHDF5();
    printf("HDF5 file closed.\n");

    // Cleanup pool
    if (thread_pool) {
        delete thread_pool;
        thread_pool = nullptr;
    }

    clearImageQueue();
    clearHDF5Queue();
}

void RGB::getLatestFrame(cv::Mat* output_frame)
{
    std::lock_guard<std::mutex> lock(display_mutex);
    cv::Mat latest_frame;
    if (display_stack.top(latest_frame) && !latest_frame.empty()) {
        *output_frame = latest_frame.clone();
    }
}

// =============================================
// Thread Functions
// =============================================

void RGB::imageCallback(unsigned char* image_data, MV_FRAME_OUT_INFO_EX* frame_info, void* user_data)
{
    if (!frame_info || !image_data || !user_data) return;

    RGB* camera = static_cast<RGB*>(user_data);
    if (camera->should_exit) return;

    ImageNode* image_node = new ImageNode();
    if (!image_node) return;

    image_node->pixel_type = frame_info->enPixelType;
    image_node->data_length = frame_info->nFrameLenEx;
    image_node->width = frame_info->nWidth;
    image_node->height = frame_info->nHeight;
    image_node->frame_number = frame_info->nFrameNum; // 记录帧号

    image_node->image_data = (unsigned char*)malloc(image_node->data_length);
    if (!image_node->image_data) {
        delete image_node;
        return;
    }
    memcpy(image_node->image_data, image_data, image_node->data_length);

    camera->image_queue.push(image_node);
    camera->image_semaphore.notify();
}

void RGB::distributeTasksThread()
{
    while (is_saving || !image_queue.empty()) {
        image_semaphore.wait(1); // wait with timeout to check is_saving

        if (should_exit && image_queue.empty()) break;

        ImageNode* image_node = nullptr;
        if (!image_queue.try_pop(image_node) || image_node == nullptr) {
            if (!is_saving && image_queue.empty()) break;
            continue;
        }

        if (thread_pool) {
            thread_pool->enqueue([this, image_node]() {
                processAndQueueFrame(image_node);
                delete image_node;
            });
        } else {
            processAndQueueFrame(image_node);
            delete image_node;
        }
    }
    printf("Task distribution thread exited.\n");
}

void RGB::processAndQueueFrame(ImageNode* image_node)
{
    size_t rgb_buffer_size = image_node->width * image_node->height * 3;
    unsigned char* local_rgb_buffer = (unsigned char*)malloc(rgb_buffer_size);
    if (!local_rgb_buffer) {
        if(image_node->image_data) free(image_node->image_data);
        image_node->image_data = nullptr;
        return;
    }

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
        free(local_rgb_buffer);
        if(image_node->image_data) free(image_node->image_data);
        image_node->image_data = nullptr;
        return;
    }

    // Create wrapper
    cv::Mat image_wrapper(image_node->height, image_node->width, CV_8UC3, local_rgb_buffer);

    // Create ProcessedFrame (Deep Copy)
    ProcessedFrame* p_frame = new ProcessedFrame();
    cv::flip(image_wrapper, p_frame->frame, 0);
    p_frame->frame_number = image_node->frame_number; // 传递帧号

    // Push to HDF5 Queue
    hdf5_write_queue.push(p_frame);

    // Push to Display Stack
    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_stack.push(p_frame->frame);
    }

    free(local_rgb_buffer);
    if(image_node->image_data) free(image_node->image_data);
    image_node->image_data = nullptr;
}

void RGB::hdf5WriteLoop()
{
    printf("HDF5 writer thread started.\n");
    while (is_saving || !hdf5_write_queue.empty())
    {
        ProcessedFrame* frame_to_save = nullptr;
        if (hdf5_write_queue.try_pop(frame_to_save)) {
            if (frame_to_save) {
                try {
                    extendAndWriteHDF5(frame_to_save);
                } catch (H5::Exception& e) {
                    printf("HDF5 write error: %s\n", e.getCDetailMsg());
                }
                delete frame_to_save;
            }
        } else {
            if (!is_saving) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    printf("HDF5 writer thread exiting.\n");
}

// =============================================
// HDF5 Implementation
// =============================================

bool RGB::initializeHDF5(const std::string& base_path)
{
    try {
        MVCC_INTVALUE width_info = { 0 }, height_info = { 0 };
        nRet = MV_CC_GetIntValue(camera_handle, "Width", &width_info);
        if (nRet != MV_OK) return false;
        nRet = MV_CC_GetIntValue(camera_handle, "Height", &height_info);
        if (nRet != MV_OK) return false;

        uint64_t width = width_info.nCurValue;
        uint64_t height = height_info.nCurValue;
        unsigned int channels = 3;

        std::string h5_filename = base_path + "/rgb_data.h5";
        h5_file = std::make_unique<H5::H5File>(h5_filename, H5F_ACC_TRUNC);

        H5::Group rgb_group = h5_file->createGroup("/rgb");

        // 1. Setup Image Dataset
        hsize_t rgb_dims[4] = { 0, (hsize_t)height, (hsize_t)width, (hsize_t)channels };
        hsize_t rgb_maxdims[4] = { H5S_UNLIMITED, (hsize_t)height, (hsize_t)width, (hsize_t)channels };
        H5::DataSpace rgb_dataspace(4, rgb_dims, rgb_maxdims);

        H5::DSetCreatPropList rgb_props;
        hsize_t chunk_dims[4] = { 1, (hsize_t)height, (hsize_t)width, (hsize_t)channels };
        rgb_props.setChunk(4, chunk_dims);
        // rgb_props.setDeflate(6); // Optional compression

        h5_rgb_dataset = rgb_group.createDataSet("frames", H5::PredType::NATIVE_UINT8, rgb_dataspace, rgb_props);
        
        h5_rgb_dims[0] = 0;
        h5_rgb_dims[1] = height;
        h5_rgb_dims[2] = width;
        h5_rgb_dims[3] = channels;

        // 2. [新增] Setup Frame Number Dataset
        hsize_t fn_dims[1] = { 0 };
        hsize_t fn_maxdims[1] = { H5S_UNLIMITED };
        H5::DataSpace fn_dataspace(1, fn_dims, fn_maxdims);

        H5::DSetCreatPropList fn_props;
        hsize_t fn_chunk_dims[1] = { 100 }; // Chunk size 100
        fn_props.setChunk(1, fn_chunk_dims);

        // Use NATIVE_UINT64 for storage to be safe and consistent with large counts
        h5_frame_num_dataset = rgb_group.createDataSet("frame_nums", H5::PredType::NATIVE_UINT64, fn_dataspace, fn_props);

    } catch (H5::Exception& e) {
        printf("Failed to initialize HDF5: %s\n", e.getCDetailMsg());
        return false;
    }
    return true;
}

void RGB::extendAndWriteHDF5(ProcessedFrame* frame)
{
    try {
        // 1. Write Image
        h5_rgb_dims[0]++;
        h5_rgb_dataset.extend(h5_rgb_dims);

        H5::DataSpace img_file_space = h5_rgb_dataset.getSpace();
        hsize_t img_offset[4] = { h5_rgb_dims[0] - 1, 0, 0, 0 };
        hsize_t img_slab_dims[4] = { 1, h5_rgb_dims[1], h5_rgb_dims[2], h5_rgb_dims[3] };
        img_file_space.selectHyperslab(H5S_SELECT_SET, img_slab_dims, img_offset);

        H5::DataSpace img_mem_space(4, img_slab_dims, NULL);
        h5_rgb_dataset.write(frame->frame.data, H5::PredType::NATIVE_UINT8, img_mem_space, img_file_space);

        // 2. [新增] Write Frame Number
        hsize_t fn_dims[1] = { h5_rgb_dims[0] };
        h5_frame_num_dataset.extend(fn_dims);

        H5::DataSpace fn_file_space = h5_frame_num_dataset.getSpace();
        hsize_t fn_offset[1] = { h5_rgb_dims[0] - 1 };
        hsize_t fn_slab_dims[1] = { 1 };
        fn_file_space.selectHyperslab(H5S_SELECT_SET, fn_slab_dims, fn_offset);

        H5::DataSpace fn_mem_space(1, fn_slab_dims, NULL);
        // Write using NATIVE_UINT (from memory) to NATIVE_UINT64 (in file)
        h5_frame_num_dataset.write(&frame->frame_number, H5::PredType::NATIVE_UINT, fn_mem_space, fn_file_space);

    } catch (H5::Exception& e) {
        printf("HDF5 extend/write error: %s\n", e.getCDetailMsg());
    }
}

void RGB::closeHDF5()
{
    std::lock_guard<std::mutex> lock(h5_mutex);
    try {
        if (h5_rgb_dataset.getId() >= 0) {
            h5_rgb_dataset.close();
            h5_rgb_dataset = H5::DataSet();
        }
        
        // [新增] Close Frame Number Dataset
        if (h5_frame_num_dataset.getId() >= 0) {
            h5_frame_num_dataset.close();
            h5_frame_num_dataset = H5::DataSet();
        }

        if (h5_file) {
            h5_file->close();
            h5_file.reset();
        }
    } catch (H5::Exception& e) {
        printf("Error closing HDF5 file: %s\n", e.getCDetailMsg());
    }
}