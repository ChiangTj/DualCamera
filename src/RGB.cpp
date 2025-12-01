#include "../include/RGB.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm> // for std::min

#ifndef MV_GENTL_USB_DEVICE
#define MV_GENTL_USB_DEVICE 0x00000008
#endif

// =============================================
// 辅助函数：获取当前纳秒时间戳
// =============================================
static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

// =============================================
// 构造与析构
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
    stopCapture();
    cleanupResources();
}

void RGB::initializeInternalParameters() {
    // 原子状态标志复位
    task_stop.store(false);
    is_initialized.store(false);
    is_saving.store(false);
    should_exit.store(false);
    is_recording.store(false);

    nRet = MV_OK;
    frame_counter = 0;
    total_frames_written = 0;

    // 指针置空
    thread_pool = nullptr;
    h5file = nullptr;
    rgb_buffer = nullptr;
    camera_handle = nullptr;

    // 清空队列
    clearImageQueue();
    {
        std::lock_guard<std::mutex> lock(hdf5_mutex);
        hdf5_image_batch.clear();
        hdf5_timestamp_batch.clear();
    }
}

// =============================================
// SDK 初始化与设备连接
// =============================================
bool RGB::initializeCameraSDK() {
    nRet = MV_CC_Initialize();
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Init SDK failed! Error: " << std::hex << nRet << std::dec << std::endl;
        return false;
    }
    return true;
}

bool RGB::enumerateAndSelectCamera() {
    // 枚举所有常见接口类型的相机
    std::memset(&device_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    nRet = MV_CC_EnumDevices(MV_GENTL_GIGE_DEVICE | MV_GENTL_USB_DEVICE | MV_GENTL_CXP_DEVICE, &device_list);

    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Enum devices failed! Error: " << std::hex << nRet << std::dec << std::endl;
        return false;
    }

    if (device_list.nDeviceNum == 0) {
        std::cerr << "[" << camera_name << "] No camera found!" << std::endl;
        return false;
    }

    // 检查索引越界
    if (camera_index < 0 || camera_index >= (int)device_list.nDeviceNum) {
        std::cerr << "[" << camera_name << "] Index " << camera_index
            << " out of range (Total: " << device_list.nDeviceNum << ")" << std::endl;
        return false;
    }

    // 创建句柄
    nRet = MV_CC_CreateHandle(&camera_handle, device_list.pDeviceInfo[camera_index]);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Create handle failed! Error: " << std::hex << nRet << std::dec << std::endl;
        return false;
    }

    // 打开设备
    nRet = MV_CC_OpenDevice(camera_handle);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Open device failed! Error: " << std::hex << nRet << std::dec << std::endl;
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
        return false;
    }

    // 尝试获取序列号用于日志打印（非必要）
    // 这里简单打印一下，确认打开的是哪台
    std::cout << "[" << camera_name << "] Opened successfully (Index: " << camera_index << ")" << std::endl;

    return true;
}

bool RGB::allocateImageBuffers() {
    // 获取 PayloadSize 仅作检查，实际转换 Buffer 在处理时动态分配或复用
    MVCC_INTVALUE stParam;
    memset(&stParam, 0, sizeof(MVCC_INTVALUE));
    nRet = MV_CC_GetIntValue(camera_handle, "PayloadSize", &stParam);
    return (MV_OK == nRet);
}

bool RGB::configureCameraSettings() {
    if (!camera_handle) return false;

    // 1. 设置触发模式为 Off (连续采集)
    nRet = MV_CC_SetEnumValue(camera_handle, "TriggerMode", 0);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Warning: Set TriggerMode Off failed." << std::endl;
    }

    // 2. 设置内部缓存节点数 (防止丢帧)
    nRet = MV_CC_SetImageNodeNum(camera_handle, nImageNodeNum);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Warning: Set ImageNodeNum failed." << std::endl;
    }

    return true;
}

// =============================================
// 参数设置接口
// =============================================
bool RGB::setExposureTime(float exposure_time_us) {
    if (!camera_handle) return false;

    // 关闭自动曝光
    nRet = MV_CC_SetEnumValue(camera_handle, "ExposureAuto", 0); // MV_EXPOSURE_AUTO_MODE_OFF

    // 设置曝光时间
    nRet = MV_CC_SetFloatValue(camera_handle, "ExposureTime", exposure_time_us);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Set ExposureTime failed: " << std::hex << nRet << std::dec << std::endl;
        return false;
    }
    std::cout << "[" << camera_name << "] Exposure set to " << exposure_time_us << " us" << std::endl;
    return true;
}

bool RGB::setGain(float gain_value) {
    if (!camera_handle) return false;

    // 关闭自动增益
    nRet = MV_CC_SetEnumValue(camera_handle, "GainAuto", 0); // MV_GAIN_MODE_OFF

    // 设置增益
    nRet = MV_CC_SetFloatValue(camera_handle, "Gain", gain_value);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Set Gain failed: " << std::hex << nRet << std::dec << std::endl;
        return false;
    }
    std::cout << "[" << camera_name << "] Gain set to " << gain_value << std::endl;
    return true;
}

// =============================================
// HDF5 文件操作 (核心逻辑)
// =============================================
bool RGB::initHDF5File() {
    // 此函数在 flushHDF5 内部调用，外部已加锁
    if (h5file) return true;

    // 确保目录存在
    QDir dir;
    if (!dir.exists(QString::fromStdString(save_folder))) {
        dir.mkpath(QString::fromStdString(save_folder));
    }

    // 文件名格式: save_path/name_YYYYMMDD_HHMMSS.h5
    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    std::ostringstream oss;
    oss << save_folder << "/" << camera_name << "_" << stamp.toStdString() << ".h5";
    h5_filename = oss.str();

    try {
        // 创建文件 (如果存在则截断覆盖)
        h5file = new H5::H5File(h5_filename, H5F_ACC_TRUNC);
        std::cout << "[" << camera_name << "] Created HDF5 file: " << h5_filename << std::endl;
    }
    catch (const H5::Exception& e) {
        std::cerr << "[" << camera_name << "] HDF5 Create Failed: " << e.getDetailMsg() << std::endl;
        if (h5file) { delete h5file; h5file = nullptr; }
        return false;
    }
    return true;
}

void RGB::closeHDF5() {
    // 强制写入剩余数据
    flushHDF5(true);

    std::lock_guard<std::mutex> lock(hdf5_mutex);
    if (h5file) {
        try {
            h5file->close();
        }
        catch (...) {}
        delete h5file;
        h5file = nullptr;
    }
    // 清空缓存
    hdf5_image_batch.clear();
    hdf5_timestamp_batch.clear();
}

void RGB::flushHDF5(bool force) {
    std::lock_guard<std::mutex> lock(hdf5_mutex);

    // 如果没有数据，直接返回
    if (hdf5_image_batch.empty()) return;

    // 初始化文件
    if (!h5file) {
        if (!initHDF5File()) return;
    }

    // 获取数据维度 (假设 batch 中所有图片尺寸一致)
    cv::Mat& first_frame = hdf5_image_batch.front();
    hsize_t H = first_frame.rows;
    hsize_t W = first_frame.cols;
    hsize_t C = first_frame.channels();
    hsize_t N_write = hdf5_image_batch.size();

    try {
        // 1. 准备/打开 Dataset
        H5::DataSet dataset;
        bool exists = false;
        try {
            dataset = h5file->openDataSet("/frames");
            exists = true;
        }
        catch (...) { exists = false; }

        if (!exists) {
            // 创建 /frames: [Unlimited, H, W, C]
            hsize_t dims[4] = { 0, H, W, C };
            hsize_t max_dims[4] = { H5S_UNLIMITED, H, W, C };
            // Chunk 大小设置 (batch size, H, W, C)
            hsize_t chunk_dims[4] = { std::min((hsize_t)hdf5_batch_size, (hsize_t)20), H, W, C };

            H5::DataSpace dataspace(4, dims, max_dims);
            H5::DSetCreatPropList prop;
            prop.setChunk(4, chunk_dims);
            // prop.setDeflate(5); // 可选：开启压缩

            dataset = h5file->createDataSet("/frames", H5::PredType::NATIVE_UCHAR, dataspace, prop);

            // 同时创建 /timestamps: [Unlimited]
            hsize_t t_dims[1] = { 0 };
            hsize_t t_max[1] = { H5S_UNLIMITED };
            hsize_t t_chunk[1] = { std::min((hsize_t)hdf5_batch_size, (hsize_t)1000) };
            H5::DataSpace t_space(1, t_dims, t_max);
            H5::DSetCreatPropList t_prop;
            t_prop.setChunk(1, t_chunk);
            h5file->createDataSet("/timestamps", H5::PredType::NATIVE_UINT64, t_space, t_prop);
        }
        else {
            dataset = h5file->openDataSet("/frames");
        }

        // 2. 准备内存数据
        // 将 batch 中的 Mat 数据拷贝到一个连续的 buffer 中
        size_t single_frame_bytes = H * W * C;
        std::vector<unsigned char> data_buffer;
        data_buffer.reserve(N_write * single_frame_bytes);

        for (const auto& mat : hdf5_image_batch) {
            if (mat.isContinuous()) {
                data_buffer.insert(data_buffer.end(), mat.data, mat.data + single_frame_bytes);
            }
            else {
                cv::Mat cont = mat.clone();
                data_buffer.insert(data_buffer.end(), cont.data, cont.data + single_frame_bytes);
            }
        }

        // 3. 写入图像数据
        H5::DataSpace filespace = dataset.getSpace();
        hsize_t curr_dims[4];
        filespace.getSimpleExtentDims(curr_dims, nullptr);
        hsize_t old_N = curr_dims[0];
        hsize_t new_N = old_N + N_write;

        // 扩展 Dataset
        hsize_t new_dims_arr[4] = { new_N, H, W, C };
        dataset.extend(new_dims_arr);

        // 选择写入区域 (Hyperslab)
        filespace = dataset.getSpace(); // Extend 后需要重新获取 space
        hsize_t offset[4] = { old_N, 0, 0, 0 };
        hsize_t count[4] = { N_write, H, W, C };
        filespace.selectHyperslab(H5S_SELECT_SET, count, offset);

        // 内存空间
        hsize_t mem_dims[4] = { N_write, H, W, C };
        H5::DataSpace memspace(4, mem_dims);

        dataset.write(data_buffer.data(), H5::PredType::NATIVE_UCHAR, memspace, filespace);

        // 4. 写入时间戳
        H5::DataSet t_dataset = h5file->openDataSet("/timestamps");
        H5::DataSpace t_filespace = t_dataset.getSpace();
        hsize_t t_curr[1];
        t_filespace.getSimpleExtentDims(t_curr, nullptr);

        hsize_t t_new[1] = { t_curr[0] + N_write };
        t_dataset.extend(t_new);

        t_filespace = t_dataset.getSpace();
        hsize_t t_offset[1] = { t_curr[0] };
        hsize_t t_count[1] = { N_write };
        t_filespace.selectHyperslab(H5S_SELECT_SET, t_count, t_offset);

        hsize_t t_mem[1] = { N_write };
        H5::DataSpace t_memspace(1, t_mem);

        // Deque 转 Vector
        std::vector<uint64_t> t_vec(hdf5_timestamp_batch.begin(), hdf5_timestamp_batch.end());
        t_dataset.write(t_vec.data(), H5::PredType::NATIVE_UINT64, t_memspace, t_filespace);

        // 5. 清理与统计
        hdf5_image_batch.clear();
        hdf5_timestamp_batch.clear();
        total_frames_written += N_write;

        // 即使不强制，也建议偶尔 Flush 到底层文件，防止 crash 丢数据
        // h5file->flush(H5F_SCOPE_GLOBAL); 

    }
    catch (const H5::Exception& e) {
        std::cerr << "[" << camera_name << "] HDF5 Write Error: " << e.getDetailMsg() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[" << camera_name << "] HDF5 Write Std Error: " << e.what() << std::endl;
    }
}

// =============================================
// 采集控制
// =============================================
void RGB::startCapture(const std::string& save_path) {
    if (is_recording.load()) {
        std::cerr << "[" << camera_name << "] Already recording." << std::endl;
        return;
    }

    // 更新保存路径 (如果有传入)
    if (!save_path.empty()) {
        save_folder = save_path;
    }

    // 初始化流程
    if (!initializeCameraSDK()) return;
    if (!enumerateAndSelectCamera()) return;
    if (!allocateImageBuffers()) return;
    if (!configureCameraSettings()) return;

    // 注册回调 (使用 SDK 的回调机制)
    nRet = MV_CC_RegisterImageCallBackEx(camera_handle, imageCallback, this);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Register callback failed! Error: " << std::hex << nRet << std::dec << std::endl;
        return;
    }

    // 启动拉流
    nRet = MV_CC_StartGrabbing(camera_handle);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Start grabbing failed! Error: " << std::hex << nRet << std::dec << std::endl;
        return;
    }

    // 创建线程池 (6个线程处理图像转换和保存)
    thread_pool = new ThreadPool(6);

    // 设置状态标志
    should_exit.store(false);
    task_stop.store(false);
    is_saving.store(true);
    is_recording.store(true);

    // 启动内部处理线程
    task_distribution_thread = std::thread(&RGB::distributeTasksThread, this);
    save_thread = std::thread(&RGB::saveImagesThread, this);

    std::cout << "[" << camera_name << "] Capture started. Saving to: " << save_folder << std::endl;
}

void RGB::stopCapture() {
    if (!is_recording.load()) return;

    std::cout << "[" << camera_name << "] Stopping capture..." << std::endl;

    // 1. 停止接收新数据
    is_recording.store(false);
    should_exit.store(true); // 通知回调丢弃新帧

    // 2. 停止 SDK 采集
    if (camera_handle) {
        MV_CC_StopGrabbing(camera_handle);
    }

    // 3. 通知并停止任务分发线程
    task_stop.store(true);
    is_saving.store(false);
    image_semaphore.notifyAll(); // 唤醒等待的线程

    if (task_distribution_thread.joinable()) {
        task_distribution_thread.join();
    }

    // 4. 销毁线程池 (等待所有正在进行的转换任务完成)
    if (thread_pool) {
        delete thread_pool;
        thread_pool = nullptr;
    }

    // 5. 停止后台定时保存线程
    if (save_thread.joinable()) {
        save_thread.join();
    }

    // 6. 清理未处理的原始数据队列
    clearImageQueue();

    // 7. 强制写入所有缓存的 HDF5 数据并关闭文件
    flushHDF5(true);
    closeHDF5();

    // 8. 关闭 SDK 设备
    if (camera_handle) {
        MV_CC_CloseDevice(camera_handle);
        MV_CC_DestroyHandle(camera_handle);
        camera_handle = nullptr;
    }

    if (rgb_buffer) {
        free(rgb_buffer);
        rgb_buffer = nullptr;
    }

    std::cout << "[" << camera_name << "] Capture stopped. Total frames saved: " << total_frames_written << std::endl;
}

// =============================================
// 数据回调与处理线程
// =============================================
void RGB::imageCallback(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) {
    if (!pData || !pFrameInfo || !pUser) return;

    RGB* self = static_cast<RGB*>(pUser);
    if (self->should_exit.load()) return;

    // 分配 ImageNode
    ImageNode* node = new ImageNode();
    if (!node) return;

    // 填充元数据
    node->width = pFrameInfo->nWidth;
    node->height = pFrameInfo->nHeight;
    node->pixel_type = pFrameInfo->enPixelType;
    node->frame_number = pFrameInfo->nFrameNum;
    node->data_length = pFrameInfo->nFrameLenEx;
    node->timestamp_ns = now_ns();

    // 拷贝图像数据 (深拷贝)
    node->image_data = (unsigned char*)malloc(node->data_length);
    if (node->image_data) {
        memcpy(node->image_data, pData, node->data_length);

        // 推入队列并通知
        self->image_queue.push(node);
        self->image_semaphore.notify();
    }
    else {
        delete node;
    }
}

void RGB::distributeTasksThread() {
    while (!task_stop.load() || !image_queue.empty()) {
        // 等待新图片，或者超时 (1秒) 检查一次退出标志
        image_semaphore.wait(1);

        if (task_stop.load() && image_queue.empty()) break;

        ImageNode* node = nullptr;
        if (image_queue.try_pop(node) && node != nullptr) {
            if (thread_pool) {
                // 将耗时的转换和保存放入线程池
                thread_pool->enqueue([this, node]() {
                    processAndSaveImage(node);
                    delete node; // 处理完后释放 node
                    });
            }
            else {
                // Fallback: 如果没有线程池则串行处理
                processAndSaveImage(node);
                delete node;
            }
        }
    }
}

void RGB::saveImagesThread() {
    // 定时检查是否需要 Flush (防止最后一点数据长时间不写入)
    // 同时也起到监控的作用
    while (is_saving.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 非强制 Flush: 只有当有数据但不足 batch 时，这里不会触发 (除非我们修改 flush 逻辑)
        // 此处通常用于定期 commit 到底层磁盘以策安全
        {
            std::lock_guard<std::mutex> lock(hdf5_mutex);
            if (!hdf5_image_batch.empty()) {
                // 这里可以选择不做任何事，依赖 processAndSaveImage 的 batch 触发
                // 也可以选择每隔几秒强制 flush 一次
            }
        }
    }
}

void RGB::processAndSaveImage(ImageNode* node) {
    if (!node || !camera_handle) return;

    // 1. 格式转换: Raw -> BGR8
    // 计算目标 BGR 大小
    unsigned int nDstBufSize = node->width * node->height * 3;
    unsigned char* pDstBuf = (unsigned char*)malloc(nDstBufSize);

    if (!pDstBuf) {
        std::cerr << "[" << camera_name << "] Malloc convert buffer failed." << std::endl;
        return;
    }

    MV_CC_PIXEL_CONVERT_PARAM stConvertParam = { 0 };
    stConvertParam.nWidth = node->width;
    stConvertParam.nHeight = node->height;
    stConvertParam.pSrcData = node->image_data;
    stConvertParam.nSrcDataLen = node->data_length;
    stConvertParam.enSrcPixelType = node->pixel_type;
    stConvertParam.enDstPixelType = PixelType_Gvsp_BGR8_Packed; // OpenCV 默认格式
    stConvertParam.pDstBuffer = pDstBuf;
    stConvertParam.nDstBufferSize = nDstBufSize;

    int nRet = MV_CC_ConvertPixelType(camera_handle, &stConvertParam);
    if (MV_OK != nRet) {
        std::cerr << "[" << camera_name << "] Pixel convert failed: " << std::hex << nRet << std::dec << std::endl;
        free(pDstBuf);
        return;
    }

    // 2. 转为 OpenCV Mat
    // 注意：构造 Mat 不会拷贝数据，所以需要 clone 出来存入队列，因为 pDstBuf 会被释放
    cv::Mat frame(node->height, node->width, CV_8UC3, pDstBuf);
    cv::Mat save_frame = frame.clone();

    free(pDstBuf); // 释放临时转换 buffer

    // 3. 存入 HDF5 Batch
    bool need_flush = false;
    {
        std::lock_guard<std::mutex> lock(hdf5_mutex);
        hdf5_image_batch.push_back(save_frame);
        hdf5_timestamp_batch.push_back(node->timestamp_ns);

        if (hdf5_image_batch.size() >= hdf5_batch_size) {
            need_flush = true;
        }
    }

    // 4. 触发写入 (Flush 内部有锁)
    if (need_flush) {
        flushHDF5(false);
    }

    // 5. 更新 UI 预览栈
    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_stack.push(save_frame);
    }
}

void RGB::clearImageQueue() {
    ImageNode* node = nullptr;
    while (image_queue.try_pop(node)) {
        if (node) delete node;
    }
}

void RGB::cleanupResources() {
    clearImageQueue();
    if (rgb_buffer) {
        free(rgb_buffer);
        rgb_buffer = nullptr;
    }
}

// =============================================
// UI 访问接口
// =============================================
void RGB::getLatestFrame(cv::Mat* output_frame) {
    std::lock_guard<std::mutex> lock(display_mutex);
    cv::Mat temp;
    if (display_stack.top(temp) && !temp.empty()) {
        *output_frame = temp.clone(); // 返回拷贝，防止 UI 线程和写入线程冲突
    }
}