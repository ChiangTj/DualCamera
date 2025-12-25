#include "../include/RGB.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

// [关键] 确保 CXP 宏定义存在 (海康 SDK 标准值)
#ifndef MV_GENTL_CXP_DEVICE
#define MV_GENTL_CXP_DEVICE 0x00000010
#endif

// =============================================
// 构造与初始化
// =============================================

RGB::RGB(int index, std::string name, const std::string& path)
    : camera_index(index),
    camera_name(name),
    save_folder(path),
    image_semaphore(0)
{
    initializeInternalParameters();
}

RGB::~RGB() {
    if (is_saving.load()) {
        stopCapture();
    }
    cleanupResources();
}

void RGB::initializeInternalParameters() {
    // 状态标志初始化
    task_stop.store(false);
    is_initialized.store(false);
    is_saving.store(false);
    should_exit.store(false);
    is_recording.store(false);

    nRet = MV_OK;
    frame_counter = 0;

    // 指针置空
    thread_pool = nullptr;
    camera_handle = nullptr;
    rgb_buffer = nullptr;

    // 清空队列
    image_queue.clear();
    hdf5_write_queue.clear();
    display_stack.clear();

    // HDF5 相关置空
    h5_file.reset();
    h5_rgb_dataset = H5::DataSet();

    // SDK 结构初始化
    memset(&device_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    memset(&pixel_convert_params, 0, sizeof(MV_CC_PIXEL_CONVERT_PARAM));
    memset(&output_frame, 0, sizeof(MV_FRAME_OUT));
    memset(&image_save_params, 0, sizeof(MV_CC_SAVE_IMAGE_PARAM));
}

// =============================================
// SDK 初始化与设备连接
// =============================================

bool RGB::initializeCameraSDK() {
    // 全局初始化，允许重复调用 (SDK内部处理引用计数)
    nRet = MV_CC_Initialize();
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Init SDK failed! Error: 0x" << std::hex << nRet << std::dec << std::endl;
        return false;
    }
    return true;
}

bool RGB::enumerateAndSelectCamera() {
    memset(&device_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    // [关键修改] 强制只枚举 CXP 设备
    // 如果你有采集卡，必须确保采集卡驱动已正确安装，且未被其他软件(MVS客户端)占用
    unsigned int nTLayerType = MV_GENTL_CXP_DEVICE;

    nRet = MV_CC_EnumDevices(nTLayerType, &device_list);

    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Enum CXP devices failed! Error: 0x" << std::hex << nRet << std::dec << std::endl;
        return false;
    }

    if (device_list.nDeviceNum == 0) {
        std::cerr << "[" << camera_name << "] No CXP camera found! (nDeviceNum=0)" << std::endl;
        std::cerr << "Hint: Close MVS Client software, Check Capture Card Driver." << std::endl;
        return false;
    }

    if (camera_index < 0 || camera_index >= (int)device_list.nDeviceNum) {
        std::cerr << "[" << camera_name << "] Index " << camera_index
            << " out of range (Total: " << device_list.nDeviceNum << ")" << std::endl;
        return false;
    }

    // 创建句柄
    nRet = MV_CC_CreateHandle(&camera_handle, device_list.pDeviceInfo[camera_index]);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Create handle failed! Error: 0x" << std::hex << nRet << std::dec << std::endl;
        return false;
    }

    // 打开设备
    nRet = MV_CC_OpenDevice(camera_handle);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Open device failed! Error: 0x" << std::hex << nRet << std::dec << std::endl;
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
        return false;
    }

    return true;
}

bool RGB::allocateImageBuffers() {
    MVCC_INTVALUE stParam;
    memset(&stParam, 0, sizeof(MVCC_INTVALUE));
    nRet = MV_CC_GetIntValue(camera_handle, "PayloadSize", &stParam);
    return (MV_OK == nRet);
}

bool RGB::configureCameraSettings() {
    if (!camera_handle) return false;
    // 设置触发模式 Off
    MV_CC_SetEnumValue(camera_handle, "TriggerMode", 0);
    // 设置缓存节点数
    MV_CC_SetImageNodeNum(camera_handle, nImageNodeNum);
    return true;
}

// =============================================
// 参数设置
// =============================================

bool RGB::setExposureTime(float exposure_time_us) {
    if (!camera_handle) return false;
    MV_CC_SetEnumValue(camera_handle, "ExposureAuto", 0);
    nRet = MV_CC_SetFloatValue(camera_handle, "ExposureTime", exposure_time_us);
    return (MV_OK == nRet);
}

bool RGB::setGain(float gain_value) {
    if (!camera_handle) return false;
    MV_CC_SetEnumValue(camera_handle, "GainAuto", 0);
    nRet = MV_CC_SetFloatValue(camera_handle, "Gain", gain_value);
    return (MV_OK == nRet);
}

// =============================================
// 资源管理
// =============================================

void RGB::cleanupResources() {
    if (rgb_buffer) {
        free(rgb_buffer);
        rgb_buffer = nullptr;
    }
    if (camera_handle) {
        MV_CC_CloseDevice(camera_handle);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
    }
    std::lock_guard<std::mutex> lock(display_mutex);
    display_stack.clear();
}

void RGB::clearImageQueue() {
    ImageNode* node = nullptr;
    while (image_queue.try_pop(node)) {
        if (node) delete node;
    }
}

void RGB::clearHDF5Queue() {
    ProcessedFrame* frame = nullptr;
    while (hdf5_write_queue.try_pop(frame)) {
        if (frame) delete frame;
    }
}

// =============================================
// 采集控制 (Start / Stop)
// =============================================

void RGB::startCapture(const std::string& save_path) {
    if (is_recording.load()) return;

    if (!save_path.empty()) {
        save_folder = save_path;
    }

    // 懒加载初始化
    if (!is_initialized.load()) {
        if (!initializeCameraSDK()) return;
        if (!enumerateAndSelectCamera()) return;
        if (!allocateImageBuffers()) return;
        if (!configureCameraSettings()) return;
        is_initialized.store(true);
    }

    // 初始化 HDF5
    if (!initializeHDF5(save_folder)) {
        std::cerr << "[" << camera_name << "] Failed to init HDF5!" << std::endl;
        return;
    }

    if (thread_pool) delete thread_pool;
    thread_pool = new ThreadPool(4);

    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
    if (MV_OK != nRet) { closeHDF5(); return; }

    nRet = MV_CC_StartGrabbing(camera_handle);
    if (MV_OK != nRet) { closeHDF5(); return; }

    should_exit.store(false);
    task_stop.store(false);
    is_saving.store(true);
    is_recording.store(true);

    hdf5_write_queue.resume();

    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);
    hdf5_writer_thread = std::thread(&RGB::hdf5WriteLoop, this);

    std::cout << "[" << camera_name << "] Capture started." << std::endl;
}

void RGB::stopCapture() {
    if (!is_recording.load()) return;
    std::cout << "[" << camera_name << "] Stopping capture..." << std::endl;

    is_recording.store(false);
    should_exit.store(true);
    is_saving.store(false);

    image_semaphore.notifyAll();
    hdf5_write_queue.stopWait();

    if (hdf5_writer_thread.joinable()) {
        hdf5_writer_thread.join();
    }
    if (task_distribution_thread.joinable()) {
        task_distribution_thread.join();
    }

    if (camera_handle) {
        MV_CC_StopGrabbing(camera_handle);
        MV_CC_RegisterImageCallBackEx(camera_handle, NULL, NULL);
    }

    if (thread_pool) {
        delete thread_pool;
        thread_pool = nullptr;
    }

    closeHDF5();
    clearImageQueue();
    clearHDF5Queue();

    std::cout << "[" << camera_name << "] Capture fully stopped." << std::endl;
}

// =============================================
// 数据回调与任务分发 (L1 -> L2)
// =============================================

void RGB::imageCallback(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) {
    if (!pData || !pFrameInfo || !pUser) return;
    RGB* self = static_cast<RGB*>(pUser);

    if (self->should_exit.load()) return;

    ImageNode* node = new ImageNode();
    if (!node) return;

    node->width = pFrameInfo->nWidth;
    node->height = pFrameInfo->nHeight;
    node->pixel_type = pFrameInfo->enPixelType;
    node->frame_number = pFrameInfo->nFrameNum;
    node->data_length = pFrameInfo->nFrameLenEx;

    node->image_data = (unsigned char*)malloc(node->data_length);
    if (node->image_data) {
        memcpy(node->image_data, pData, node->data_length);
        self->image_queue.push(node);
        self->image_semaphore.notify();
    }
    else {
        delete node;
    }
}

void RGB::distributeTasksThread() {
    while (is_saving.load() || !image_queue.empty()) {
        image_semaphore.wait(1);
        if (should_exit.load() && image_queue.empty()) break;

        ImageNode* node = nullptr;
        if (image_queue.try_pop(node)) {
            if (thread_pool) {
                thread_pool->enqueue([this, node]() {
                    processAndQueueFrame(node);
                    delete node;
                    });
            }
            else {
                processAndQueueFrame(node);
                delete node;
            }
        }
    }
}

// =============================================
// 图像处理 (L2 -> L3)
// =============================================

void RGB::processAndQueueFrame(ImageNode* node) {
    if (!node || !camera_handle) return;

    unsigned int nDstBufSize = node->width * node->height * 3;
    unsigned char* pDstBuf = (unsigned char*)malloc(nDstBufSize);
    if (!pDstBuf) return;

    MV_CC_PIXEL_CONVERT_PARAM stConvertParam = { 0 };
    stConvertParam.nWidth = node->width;
    stConvertParam.nHeight = node->height;
    stConvertParam.pSrcData = node->image_data;
    stConvertParam.nSrcDataLen = node->data_length;
    stConvertParam.enSrcPixelType = node->pixel_type;
    stConvertParam.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
    stConvertParam.pDstBuffer = pDstBuf;
    stConvertParam.nDstBufferSize = nDstBufSize;

    int nRet = MV_CC_ConvertPixelType(camera_handle, &stConvertParam);
    if (MV_OK == nRet) {
        cv::Mat wrapper(node->height, node->width, CV_8UC3, pDstBuf);

        // 深拷贝1：给写入队列
        ProcessedFrame* p_frame = new ProcessedFrame();
        p_frame->frame = wrapper.clone();
        p_frame->frame_number = node->frame_number;
        hdf5_write_queue.push(p_frame);

        // 深拷贝2：给显示栈
        {
            std::lock_guard<std::mutex> lock(display_mutex);
            display_stack.push(wrapper.clone());
        }
    }
    free(pDstBuf);
}

// =============================================
// HDF5 写入循环 (L4)
// =============================================

void RGB::hdf5WriteLoop() {
    while (is_saving.load() || !hdf5_write_queue.empty()) {
        ProcessedFrame* frame = nullptr;
        if (hdf5_write_queue.wait_pop(frame)) {
            if (frame) {
                try {
                    extendAndWriteHDF5(frame);
                }
                catch (H5::Exception& e) {
                    std::cerr << "[" << camera_name << "] H5 Write Error: " << e.getCDetailMsg() << std::endl;
                }
                delete frame;
            }
        }
    }
}

// =============================================
// HDF5 辅助函数
// =============================================

bool RGB::initializeHDF5(const std::string& base_path) {
    try {
        MVCC_INTVALUE w = { 0 }, h = { 0 };
        nRet = MV_CC_GetIntValue(camera_handle, "Width", &w);
        if (nRet != MV_OK) return false;
        nRet = MV_CC_GetIntValue(camera_handle, "Height", &h);
        if (nRet != MV_OK) return false;

        QDir().mkpath(QString::fromStdString(base_path));
        std::string h5_filename = base_path + "/rgb_data.h5";

        h5_file = std::make_unique<H5::H5File>(h5_filename, H5F_ACC_TRUNC);
        H5::Group rgb_group = h5_file->createGroup("/rgb");

        hsize_t dims[4] = { 0, (hsize_t)h.nCurValue, (hsize_t)w.nCurValue, 3 };
        hsize_t maxdims[4] = { H5S_UNLIMITED, (hsize_t)h.nCurValue, (hsize_t)w.nCurValue, 3 };

        H5::DataSpace dataspace(4, dims, maxdims);
        H5::DSetCreatPropList props;

        // 使用 static_cast 消除编译器警告
        hsize_t chunk[4] = { 1, (hsize_t)h.nCurValue, (hsize_t)w.nCurValue, 3 };
        props.setChunk(4, chunk);

        h5_rgb_dataset = rgb_group.createDataSet("frames", H5::PredType::NATIVE_UINT8, dataspace, props);

        h5_rgb_dims[0] = 0;
        h5_rgb_dims[1] = h.nCurValue;
        h5_rgb_dims[2] = w.nCurValue;
        h5_rgb_dims[3] = 3;

    }
    catch (H5::Exception& e) {
        std::cerr << "[" << camera_name << "] H5 Init Fail: " << e.getCDetailMsg() << std::endl;
        return false;
    }
    return true;
}

void RGB::extendAndWriteHDF5(ProcessedFrame* frame) {
    h5_rgb_dims[0]++;
    h5_rgb_dataset.extend(h5_rgb_dims);

    H5::DataSpace file_space = h5_rgb_dataset.getSpace();
    hsize_t offset[4] = { h5_rgb_dims[0] - 1, 0, 0, 0 };
    hsize_t count[4] = { 1, (hsize_t)frame->frame.rows, (hsize_t)frame->frame.cols, 3 };
    file_space.selectHyperslab(H5S_SELECT_SET, count, offset);

    H5::DataSpace mem_space(4, count, NULL);
    h5_rgb_dataset.write(frame->frame.data, H5::PredType::NATIVE_UINT8, mem_space, file_space);
}

void RGB::closeHDF5() {
    std::lock_guard<std::mutex> lock(h5_mutex);
    try {
        if (h5_rgb_dataset.getId() >= 0) {
            h5_rgb_dataset.close();
            h5_rgb_dataset = H5::DataSet();
        }
        if (h5_file) {
            h5_file->close();
            h5_file.reset();
        }
    }
    catch (H5::Exception& e) {
        std::cerr << "Error closing H5: " << e.getCDetailMsg() << std::endl;
    }
}

void RGB::getLatestFrame(cv::Mat* output_frame) {
    std::lock_guard<std::mutex> lock(display_mutex);
    cv::Mat temp;
    if (display_stack.top(temp) && !temp.empty()) {
        *output_frame = temp.clone();
    }
}