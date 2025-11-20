#include "../include/DataProcessor.h"
#include <QDir>
#include <QDebug>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring> // for memset
#include <filesystem> // for file_size
#include <omp.h>

// =========================================================
// 辅助宏：性能计时
// =========================================================
#define TICK(name) auto t_##name = std::chrono::high_resolution_clock::now();
#define TOCK(name, key) \
    auto d_##name = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - t_##name).count(); \
    m_profiler.addRecord(key, d_##name / 1000.0);

// =========================================================
// 构造与析构
// =========================================================
DataProcessor::DataProcessor(const std::string& segmentPath,
    const cv::Mat& homographyMatrix,
    QObject* parent)
    : QObject(parent),
    m_segmentPath(segmentPath),
    m_homo(homographyMatrix),
    m_numFrames(0)
{
    m_segmentName = QDir(QString::fromStdString(m_segmentPath)).dirName().toStdString();
}

DataProcessor::~DataProcessor() {
    qInfo() << "DataProcessor Destroyed.";
}

// =========================================================
// 主处理流程
// =========================================================
void DataProcessor::process()
{
    m_profiler.clear(); // 清除旧的性能数据

    try {
        // --- Step 1: 加载 RAW 数据 (包含分辨率自适应检测) ---
        emit progress("Step 1/4: Loading RAW data...");
        TICK(load_raw);
        if (!loadFromRaw()) throw std::runtime_error("Failed to load RAW or align frames.");
        TOCK(load_raw, "Step_LoadRaw");

        // --- Step 2: 预计算事件索引 (CPU 优化: 滑动窗口) ---
        emit progress("Step 2/4: Pre-calculating Event Indices...");
        TICK(pre_index);

        m_frameEventIndices.resize(m_numFrames);
        size_t current_idx = 0;
        size_t total_events = m_events.size();

        for (int i = 0; i < m_numFrames; ++i) {
            if (i + 1 >= m_triggers.size()) break;

            uint64_t t_start = m_triggers[i].t;
            uint64_t t_end = m_triggers[i + 1].t;

            // 寻找起点 (O(1) 均摊复杂度)
            while (current_idx < total_events && m_events[current_idx].t < t_start) {
                current_idx++;
            }
            size_t start_idx = current_idx;

            // 寻找终点
            size_t temp_idx = start_idx;
            while (temp_idx < total_events && m_events[temp_idx].t < t_end) {
                temp_idx++;
            }
            size_t end_idx = temp_idx; // 左闭右开

            m_frameEventIndices[i] = std::make_pair(start_idx, end_idx);
        }
        TOCK(pre_index, "Step_PreIndex");

        // --- Step 3: 初始化 HDF5 输出 ---
        emit progress("Step 3/4: Creating HDF5 output...");
        if (!createOutputH5()) throw std::runtime_error("Failed to create HDF5.");

        // --- Step 4: 分块并行处理 (查表法 + OpenMP) ---
        emit progress(QString("Step 4/4: Processing %1 frames (Lookup Table Remap)...").arg(m_numFrames));
        TICK(process_chunked);
        if (!processFramesChunked()) throw std::runtime_error("Error in chunked processing.");
        TOCK(process_chunked, "Step_TotalProcess");

        // 清理资源
        if (m_outputFile) m_outputFile->close();

        // --- 输出性能报告 ---
        QString report = m_profiler.getReport();
        qInfo().noquote() << report;

        emit progress("Processing Complete. Check console for perf stats.");
        emit finished(true);
    }
    catch (const std::exception& e) {
        qWarning() << "Error:" << e.what();
        emit progress(QString("Error: %1").arg(e.what()));
        emit finished(false);
    }
    catch (...) {
        emit finished(false);
    }
}

// =========================================================
// Step 1 实现: 智能加载与对齐
// =========================================================
bool DataProcessor::loadFromRaw()
{
    // 1. 先读取 RGB HDF5 获取真实的帧数和分辨率
    // 防止硬编码分辨率与实际文件不符导致 Y 轴翻转错误
    std::string rgb_h5_path = m_segmentPath + "/rgb_data.h5";
    int rgb_count = 0;
    int real_h = 0;
    int real_w = 0;

    try {
        H5::H5File f(rgb_h5_path, H5F_ACC_RDONLY);
        H5::DataSet ds = f.openDataSet("rgb/frames");
        hsize_t dims[4];
        ds.getSpace().getSimpleExtentDims(dims, NULL);

        rgb_count = (int)dims[0];
        real_h = (int)dims[1];
        real_w = (int)dims[2];

        qInfo() << "Detected RGB H5:" << real_w << "x" << real_h << "Frames:" << rgb_count;

        if (rgb_count == 0) {
            qCritical() << "Error: RGB file has 0 frames (Zombie File).";
            return false;
        }
    }
    catch (...) {
        qCritical() << "Failed to open/read RGB H5 file.";
        return false;
    }

    // 2. 加载 RAW 事件数据
    std::string raw_path = m_segmentPath + "/" + m_segmentName + ".raw";
    if (!QFile::exists(QString::fromStdString(raw_path))) return false;

    try {
        auto fsize = std::filesystem::file_size(raw_path);
        // 预估事件数量进行 reserve，避免 realloc
        m_events.reserve(fsize / 8);

        Metavision::Camera cam = Metavision::Camera::from_file(
            raw_path,
            Metavision::FileConfigHints().real_time_playback(false)
        );

        // 读取 CD 事件
        cam.cd().add_callback([this, real_h](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
            for (const auto* ev = begin; ev != end; ++ev) {
                // 使用真实高度进行 Y 轴翻转计算，防止下溢
                uint32_t y_coord = (real_h > ev->y) ? (real_h - 1 - ev->y) : 0;
                m_events.push_back(Event{
                    (uint64_t)ev->t, (uint32_t)ev->x, y_coord, (bool)ev->p
                    });
            }
            });

        // 读取 Trigger 信号
        cam.ext_trigger().add_callback([this](const Metavision::EventExtTrigger* begin, const Metavision::EventExtTrigger* end) {
            for (const auto* ev = begin; ev != end; ++ev) {
                if (ev->p == 0) { // 硬件极性修正
                    m_triggers.push_back(Trigger{ (uint64_t)ev->t, ev->id, (bool)ev->p });
                }
            }
            });

        cam.start();
        while (cam.is_running()) QThread::msleep(10);
        cam.stop();
    }
    catch (...) { return false; }

    // 按时间排序 (确保有序性)
    std::sort(m_events.begin(), m_events.end(), [](const Event& a, const Event& b) {
        return a.t < b.t;
        });

    // 计算有效帧数 (取 Trigger 和 RGB 的交集)
    if (m_triggers.size() < 2) return false;
    m_numFrames = std::min(rgb_count, (int)m_triggers.size() - 1);

    return (m_numFrames > 0);
}

// =========================================================
// Step 2 实现: HDF5 创建
// =========================================================
bool DataProcessor::createOutputH5()
{
    try {
        std::string out_path = m_segmentPath + "/processed_data.h5";
        m_outputFile = std::make_unique<H5::H5File>(out_path, H5F_ACC_TRUNC);

        // 输出对齐后的 RGB
        hsize_t rgb_dims[4] = { (hsize_t)m_numFrames, (hsize_t)ALIGNED_RGB_H, (hsize_t)ALIGNED_RGB_W, 3 };
        m_rgbOutputDataset = m_outputFile->createDataSet("rgb_aligned", H5::PredType::NATIVE_UINT8, H5::DataSpace(4, rgb_dims));

        // 输出 Voxel Grid
        hsize_t vox_dims[4] = { (hsize_t)m_numFrames, (hsize_t)VOXEL_BINS, (hsize_t)VOXEL_H, (hsize_t)VOXEL_W };
        m_voxelOutputDataset = m_outputFile->createDataSet("event_voxels", H5::PredType::NATIVE_FLOAT, H5::DataSpace(4, vox_dims));
        return true;
    }
    catch (...) { return false; }
}

// =========================================================
// Step 3 实现: 终极优化处理 (查表 + 内存复用 + OpenMP)
// =========================================================
bool DataProcessor::processFramesChunked()
{
    std::string rgb_h5_path = m_segmentPath + "/rgb_data.h5";
    H5::H5File rgb_inputFile(rgb_h5_path, H5F_ACC_RDONLY);
    H5::DataSet rgb_inputDataset = rgb_inputFile.openDataSet("rgb/frames");

    // 数据大小计算 (使用 500W 适配常量)
    size_t raw_rgb_size = (size_t)INPUT_RGB_H * INPUT_RGB_W * 3;
    size_t aligned_rgb_size = (size_t)ALIGNED_RGB_H * ALIGNED_RGB_W * 3;
    size_t voxel_size = (size_t)VOXEL_BINS * VOXEL_H * VOXEL_W;

    // -------------------------------------------------------
    // [优化核心 1] 预计算映射表 (Remap Table)
    // -------------------------------------------------------
    TICK(calc_remap_table);

    // 1. 构造包含平移的优化矩阵 (Src -> Cropped Dst)
    // 目的：将裁剪区域的左上角移动到 (0,0)
    cv::Mat T = cv::Mat::eye(3, 3, CV_64F);
    T.at<double>(0, 2) = -VOXEL_CROP_X_MIN;
    T.at<double>(1, 2) = 0;
    cv::Mat H_opt = T * m_homo;

    // 2. 计算逆矩阵 (Cropped Dst -> Src)
    // Remap 需要的是反向映射：对于每一个输出像素 (x,y)，它在原图的哪里？
    cv::Mat H_inv = H_opt.inv();

    // 3. 生成查找表
    // 尺寸直接为最终输出尺寸 (1000x720)
    cv::Mat map_x(ALIGNED_RGB_H, ALIGNED_RGB_W, CV_32FC1);
    cv::Mat map_y(ALIGNED_RGB_H, ALIGNED_RGB_W, CV_32FC1);

    for (int y = 0; y < ALIGNED_RGB_H; ++y) {
        for (int x = 0; x < ALIGNED_RGB_W; ++x) {
            // 齐次坐标逆变换: P_src = H_inv * P_dst
            double src_z = H_inv.at<double>(2, 0) * x + H_inv.at<double>(2, 1) * y + H_inv.at<double>(2, 2);
            double scale = (src_z != 0) ? 1.0 / src_z : 1.0;

            double src_x = (H_inv.at<double>(0, 0) * x + H_inv.at<double>(0, 1) * y + H_inv.at<double>(0, 2)) * scale;
            double src_y = (H_inv.at<double>(1, 0) * x + H_inv.at<double>(1, 1) * y + H_inv.at<double>(1, 2)) * scale;

            map_x.at<float>(y, x) = static_cast<float>(src_x);
            map_y.at<float>(y, x) = static_cast<float>(src_y);
        }
    }
    TOCK(calc_remap_table, "Init_RemapTable");

    // -------------------------------------------------------
    // [优化核心 2] 内存复用 (Memory Reuse)
    // -------------------------------------------------------
    // 将 vector 定义移到循环外，避免在循环中重复 malloc/free
    std::vector<uint8_t> chunk_raw_rgb;
    std::vector<uint8_t> chunk_out_rgb;
    std::vector<float> chunk_out_voxels;

    try {
        chunk_raw_rgb.resize(CHUNK_SIZE * raw_rgb_size);
        chunk_out_rgb.resize(CHUNK_SIZE * aligned_rgb_size);
        chunk_out_voxels.resize(CHUNK_SIZE * voxel_size);
    }
    catch (const std::bad_alloc&) {
        qCritical() << "OOM: Failed to allocate buffers. Reduce CHUNK_SIZE.";
        return false;
    }

    // --- 外层循环：按块处理 ---
    for (int chunk_start = 0; chunk_start < m_numFrames; chunk_start += CHUNK_SIZE)
    {
        int current_chunk_size = std::min(CHUNK_SIZE, m_numFrames - chunk_start);

        // 1. IO 读取 (串行)
        TICK(io_read);
        hsize_t read_offset[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t read_count[4] = { (hsize_t)current_chunk_size, (hsize_t)INPUT_RGB_H, (hsize_t)INPUT_RGB_W, 3 };

        H5::DataSpace file_space = rgb_inputDataset.getSpace();
        file_space.selectHyperslab(H5S_SELECT_SET, read_count, read_offset);
        H5::DataSpace mem_space(4, read_count, NULL);

        rgb_inputDataset.read(chunk_raw_rgb.data(), H5::PredType::NATIVE_UINT8, mem_space, file_space);
        TOCK(io_read, "Chunk_IO_Read");

        // 2. 并行计算 (Remap + Voxel)
#pragma omp parallel for
        for (int i = 0; i < current_chunk_size; ++i)
        {
            int global_frame_idx = chunk_start + i;

            // 指针定位
            uint8_t* ptr_raw = chunk_raw_rgb.data() + i * raw_rgb_size;
            uint8_t* ptr_out_rgb = chunk_out_rgb.data() + i * aligned_rgb_size;
            float* ptr_out_vox = chunk_out_voxels.data() + i * voxel_size;

            std::memset(ptr_out_vox, 0, voxel_size * sizeof(float));

            // [优化核心 3] 使用 remap 替代 warpPerspective
            // 查表法极快，无浮点矩阵运算
            TICK(remap);
            cv::Mat raw_mat(INPUT_RGB_H, INPUT_RGB_W, CV_8UC3, ptr_raw);
            cv::Mat out_mat(ALIGNED_RGB_H, ALIGNED_RGB_W, CV_8UC3, ptr_out_rgb);

            // map_x 和 map_y 是只读的，多线程安全
            cv::remap(raw_mat, out_mat, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
            TOCK(remap, "Core_Remap");

            // Voxel 处理
            TICK(voxel);
            size_t start_idx = m_frameEventIndices[global_frame_idx].first;
            size_t end_idx = m_frameEventIndices[global_frame_idx].second;
            uint64_t t_trig_start = m_triggers[global_frame_idx].t;
            uint64_t t_trig_end = m_triggers[global_frame_idx + 1].t;

            runVoxelization(start_idx, end_idx, ptr_out_vox, t_trig_start, t_trig_end);
            TOCK(voxel, "Core_Voxel");
        }

        // 3. IO 写入 (串行)
        TICK(io_write);
        // 写入 RGB
        hsize_t write_offset_rgb[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t write_count_rgb[4] = { (hsize_t)current_chunk_size, (hsize_t)ALIGNED_RGB_H, (hsize_t)ALIGNED_RGB_W, 3 };
        H5::DataSpace fspace_rgb = m_rgbOutputDataset.getSpace();
        fspace_rgb.selectHyperslab(H5S_SELECT_SET, write_count_rgb, write_offset_rgb);
        H5::DataSpace mspace_rgb(4, write_count_rgb, NULL);
        m_rgbOutputDataset.write(chunk_out_rgb.data(), H5::PredType::NATIVE_UINT8, mspace_rgb, fspace_rgb);

        // 写入 Voxel
        hsize_t write_offset_vox[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t write_count_vox[4] = { (hsize_t)current_chunk_size, (hsize_t)VOXEL_BINS, (hsize_t)VOXEL_H, (hsize_t)VOXEL_W };
        H5::DataSpace fspace_vox = m_voxelOutputDataset.getSpace();
        fspace_vox.selectHyperslab(H5S_SELECT_SET, write_count_vox, write_offset_vox);
        H5::DataSpace mspace_vox(4, write_count_vox, NULL);
        m_voxelOutputDataset.write(chunk_out_voxels.data(), H5::PredType::NATIVE_FLOAT, mspace_vox, fspace_vox);
        TOCK(io_write, "Chunk_IO_Write");

        emit progress(QString("Processed %1 / %2 frames...").arg(chunk_start + current_chunk_size).arg(m_numFrames));
    }

    return true;
}

// =========================================================
// 算法: 体素化 (标准实现)
// =========================================================
void DataProcessor::runVoxelization(size_t start_idx, size_t end_idx, float* out_voxel_ptr, uint64_t t_trigger_start, uint64_t t_trigger_end)
{
    if (start_idx >= end_idx) return;

    double deltaT = (double)(t_trigger_end - t_trigger_start);
    if (deltaT <= 0) deltaT = 1.0;
    double time_norm_factor = (double)(VOXEL_BINS - 1) / deltaT;
    int frame_pixel_count = VOXEL_H * VOXEL_W;

    for (size_t i = start_idx; i < end_idx; ++i) {
        const Event& ev = m_events[i];

        // 空间过滤 (Crop)
        if (ev.x < (uint32_t)VOXEL_CROP_X_MIN) continue;
        int x = (int)ev.x - VOXEL_CROP_X_MIN;
        int y = (int)ev.y;

        // 边界检查
        if (x >= VOXEL_W || y >= VOXEL_H || x < 0 || y < 0) continue;

        float polarity = ev.p ? 1.0f : -1.0f;
        double t_norm = (double)(ev.t - t_trigger_start) * time_norm_factor;

        int t_idx = (int)std::floor(t_norm);
        float t_weight_right = (float)(t_norm - t_idx);
        float t_weight_left = 1.0f - t_weight_right;

        int spatial_idx = y * VOXEL_W + x;

        // 双线性插值写入
        if (t_idx >= 0 && t_idx < VOXEL_BINS) {
            out_voxel_ptr[t_idx * frame_pixel_count + spatial_idx] += polarity * t_weight_left;
        }
        if (t_idx + 1 >= 0 && t_idx + 1 < VOXEL_BINS) {
            out_voxel_ptr[(t_idx + 1) * frame_pixel_count + spatial_idx] += polarity * t_weight_right;
        }
    }

    // 归一化 (Mean-Std)
    int total_size = VOXEL_BINS * frame_pixel_count;
    double sum = 0.0, sum_sq = 0.0;
    int num_nonzeros = 0;

    for (int i = 0; i < total_size; ++i) {
        float val = out_voxel_ptr[i];
        if (val != 0.0f) {
            sum += val;
            sum_sq += (val * val);
            num_nonzeros++;
        }
    }

    if (num_nonzeros > 0) {
        double mean = sum / num_nonzeros;
        double variance = (sum_sq / num_nonzeros) - (mean * mean);
        double stddev = (variance > 0) ? std::sqrt(variance) : 1.0;

        for (int i = 0; i < total_size; ++i) {
            if (out_voxel_ptr[i] != 0.0f) {
                out_voxel_ptr[i] = (float)((out_voxel_ptr[i] - mean) / stddev);
            }
        }
    }
}