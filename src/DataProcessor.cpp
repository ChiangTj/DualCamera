#include "../include/DataProcessor.h"
#include <QDir>
#include <QDebug>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring> // for memset
#include <filesystem> // for file_size
#include <omp.h>

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

void DataProcessor::process()
{
    try {
        // =========================================================
        // Step 1: 加载 RAW 数据
        // =========================================================
        emit progress("Step 1/4: Loading RAW data (Optimized)...");
        if (!loadFromRaw()) throw std::runtime_error("Failed to load RAW or align frames.");

        // =========================================================
        // Step 2: [新增优化] 预计算每帧对应的事件索引范围
        // =========================================================
        emit progress("Step 2/4: Pre-calculating Event Indices (CPU Optimization)...");

        m_frameEventIndices.resize(m_numFrames);

        // 使用双指针滑动窗口算法，复杂度 O(N)，消除内层循环的二分查找
        size_t current_idx = 0;
        size_t total_events = m_events.size();

        for (int i = 0; i < m_numFrames; ++i) {
            // 获取当前帧由 Trigger 决定的绝对时间范围
            // 注意：这里假设 triggers 数量至少为 m_numFrames + 1
            if (i + 1 >= m_triggers.size()) break;

            uint64_t t_start = m_triggers[i].t;
            uint64_t t_end = m_triggers[i + 1].t;

            // 1. 寻找起点 (从上一次结束的位置继续向后找，单调递增)
            while (current_idx < total_events && m_events[current_idx].t < t_start) {
                current_idx++;
            }
            size_t start_idx = current_idx;

            // 2. 寻找终点
            size_t temp_idx = start_idx;
            while (temp_idx < total_events && m_events[temp_idx].t < t_end) {
                temp_idx++;
            }
            size_t end_idx = temp_idx; // 左闭右开区间 [start, end)

            // 3. 保存索引
            m_frameEventIndices[i] = std::make_pair(start_idx, end_idx);

            // 下一帧的搜索从 start_idx (或 end_idx) 开始均可
            // 考虑到快门可能有重叠或间隙，保持 current_idx 在 start_idx 是安全的
        }

        // =========================================================
        // Step 3: 初始化 HDF5 输出
        // =========================================================
        emit progress("Step 3/4: Creating HDF5 output...");
        if (!createOutputH5()) throw std::runtime_error("Failed to create HDF5.");

        // =========================================================
        // Step 4: 分块并行处理 (核心计算)
        // =========================================================
        emit progress(QString("Step 4/4: Processing %1 frames (Chunked Parallel)...").arg(m_numFrames));
        if (!processFramesChunked()) throw std::runtime_error("Error in chunked processing.");

        if (m_outputFile) m_outputFile->close();
        emit progress("Processing Complete.");
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
// Step 1 实现: 高性能 RAW 加载
// =========================================================
bool DataProcessor::loadFromRaw()
{
    std::string raw_path = m_segmentPath + "/" + m_segmentName + ".raw";
    if (!QFile::exists(QString::fromStdString(raw_path))) return false;

    try {
        // --- 1. 智能内存预分配 ---
        // file_size / 8 是一个安全的上限 (EVT3 格式)
        auto fsize = std::filesystem::file_size(raw_path);
        size_t estimated_events = fsize / 8;
        m_events.reserve(estimated_events); // 关键：一次性分配，避免亿级扩容

        // --- 2. 极速读取 ---
        Metavision::Camera cam = Metavision::Camera::from_file(
            raw_path,
            Metavision::FileConfigHints().real_time_playback(false)
        );

        // CD 回调：读取事件
        cam.cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
            for (const auto* ev = begin; ev != end; ++ev) {
                // 注意：这里使用 header 中定义的 INPUT_RGB_H 进行坐标系转换（如果需要 Y 翻转）
                // 如果不需要翻转，直接用 ev->y
                m_events.push_back(Event{
                    (uint64_t)ev->t, (uint32_t)ev->x, (uint32_t)(INPUT_RGB_H - 1 - ev->y), (bool)ev->p
                    });
            }
            });

        // Trigger 回调：读取帧同步信号
        cam.ext_trigger().add_callback([this](const Metavision::EventExtTrigger* begin, const Metavision::EventExtTrigger* end) {
            for (const auto* ev = begin; ev != end; ++ev) {
                if (ev->p == 0) { // 硬件极性反转修正 (根据实际硬件调整)
                    m_triggers.push_back(Trigger{ (uint64_t)ev->t, ev->id, (bool)ev->p });
                }
            }
            });

        cam.start();
        while (cam.is_running()) QThread::msleep(10);
        cam.stop();

    }
    catch (...) { return false; }

    qInfo() << "Loaded" << m_events.size() << "events and" << m_triggers.size() << "triggers.";

    // 确保时间有序 (通常 RAW 已经有序，但为了安全)
    // 使用并行排序加速
    // std::sort(std::execution::par, m_events.begin(), m_events.end(), ...); // C++17 needed
    std::sort(m_events.begin(), m_events.end(), [](const Event& a, const Event& b) {
        return a.t < b.t;
        });

    // --- 3. 帧数校验 (与 RGB HDF5 对齐) ---
    std::string rgb_h5_path = m_segmentPath + "/rgb_data.h5";
    int rgb_count = 0;
    try {
        H5::H5File f(rgb_h5_path, H5F_ACC_RDONLY);
        H5::DataSet ds = f.openDataSet("rgb/frames");
        hsize_t dims[4];
        ds.getSpace().getSimpleExtentDims(dims, NULL);
        rgb_count = (int)dims[0];
    }
    catch (...) { return false; }

    if (m_triggers.size() < 2) return false;

    // 取 RGB 帧数和 Trigger 数的较小值，防止越界
    m_numFrames = std::min(rgb_count, (int)m_triggers.size() - 1);

    return (m_numFrames > 0);
}

// =========================================================
// Step 2 实现: 创建 HDF5 结构
// =========================================================
bool DataProcessor::createOutputH5()
{
    try {
        std::string out_path = m_segmentPath + "/processed_data.h5";
        m_outputFile = std::make_unique<H5::H5File>(out_path, H5F_ACC_TRUNC);

        // RGB Dataset: [N, H, W, 3]
        hsize_t rgb_dims[4] = { (hsize_t)m_numFrames, (hsize_t)ALIGNED_RGB_H, (hsize_t)ALIGNED_RGB_W, 3 };
        m_rgbOutputDataset = m_outputFile->createDataSet("rgb_aligned", H5::PredType::NATIVE_UINT8, H5::DataSpace(4, rgb_dims));

        // Voxel Dataset: [N, Bins, H, W]
        hsize_t vox_dims[4] = { (hsize_t)m_numFrames, (hsize_t)VOXEL_BINS, (hsize_t)VOXEL_H, (hsize_t)VOXEL_W };
        m_voxelOutputDataset = m_outputFile->createDataSet("event_voxels", H5::PredType::NATIVE_FLOAT, H5::DataSpace(4, vox_dims));
        return true;
    }
    catch (...) { return false; }
}

// =========================================================
// Step 3 实现: 分块并行处理 (核心优化)
// =========================================================
bool DataProcessor::processFramesChunked()
{
    std::string rgb_h5_path = m_segmentPath + "/rgb_data.h5";
    H5::H5File rgb_inputFile(rgb_h5_path, H5F_ACC_RDONLY);
    H5::DataSet rgb_inputDataset = rgb_inputFile.openDataSet("rgb/frames");

    // 预计算大小 (字节)
    // 使用 Header 中的常量，适应 500W 像素
    size_t raw_rgb_size = (size_t)INPUT_RGB_H * INPUT_RGB_W * 3;
    size_t aligned_rgb_size = (size_t)ALIGNED_RGB_H * ALIGNED_RGB_W * 3;
    size_t voxel_size = (size_t)VOXEL_BINS * VOXEL_H * VOXEL_W; // floats

    // --- 外层循环：按块处理 (Serial IO) ---
    // CHUNK_SIZE 在 header 中定义 (建议为 50)
    for (int chunk_start = 0; chunk_start < m_numFrames; chunk_start += CHUNK_SIZE)
    {
        int current_chunk_size = std::min(CHUNK_SIZE, m_numFrames - chunk_start);

        // 1. 内存池分配
        // 使用 vector 自动管理内存，并在 try-catch 中捕获 bad_alloc
        std::vector<uint8_t> chunk_raw_rgb;
        std::vector<uint8_t> chunk_out_rgb;
        std::vector<float> chunk_out_voxels;

        try {
            chunk_raw_rgb.resize(current_chunk_size * raw_rgb_size);
            chunk_out_rgb.resize(current_chunk_size * aligned_rgb_size);
            chunk_out_voxels.resize(current_chunk_size * voxel_size);
        }
        catch (const std::bad_alloc& e) {
            qCritical() << "Memory allocation failed for chunk logic. Reduce CHUNK_SIZE.";
            return false;
        }

        // 2. 批量读取 RGB (HDF5 IO)
        hsize_t read_offset[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t read_count[4] = { (hsize_t)current_chunk_size, (hsize_t)INPUT_RGB_H, (hsize_t)INPUT_RGB_W, 3 };

        H5::DataSpace file_space = rgb_inputDataset.getSpace();
        file_space.selectHyperslab(H5S_SELECT_SET, read_count, read_offset);
        H5::DataSpace mem_space(4, read_count, NULL);

        rgb_inputDataset.read(chunk_raw_rgb.data(), H5::PredType::NATIVE_UINT8, mem_space, file_space);

        // 3. 块内并行计算 (OpenMP)
        // 开启并行，shared 变量自动处理，i 为 private
#pragma omp parallel for
        for (int i = 0; i < current_chunk_size; ++i)
        {
            int global_frame_idx = chunk_start + i;

            // --- A. 指针定位 ---
            uint8_t* ptr_raw = chunk_raw_rgb.data() + i * raw_rgb_size;
            uint8_t* ptr_out_rgb = chunk_out_rgb.data() + i * aligned_rgb_size;
            float* ptr_out_vox = chunk_out_voxels.data() + i * voxel_size;

            // --- B. Voxel 内存清零 ---
            std::memset(ptr_out_vox, 0, voxel_size * sizeof(float));

            // --- C. 处理 RGB (Warp + Crop) ---
            // 构造 Mat 包装器 (不分配内存)
            cv::Mat raw_mat(INPUT_RGB_H, INPUT_RGB_W, CV_8UC3, ptr_raw);

            // 线程局部 Mat，用于 Warp 结果
            // warpPerspective 需要目标内存，create 会在 Mat 内部管理
            cv::Mat warped_mat;
            cv::warpPerspective(raw_mat, warped_mat, m_homo, cv::Size(INPUT_RGB_W, INPUT_RGB_H));

            // 裁剪 ROI
            // 注意：Rect 范围必须在图像内，否则抛出异常
            cv::Mat crop_roi = warped_mat(cv::Rect(VOXEL_CROP_X_MIN, 0, ALIGNED_RGB_W, ALIGNED_RGB_H));

            // 复制到输出 Buffer
            cv::Mat out_mat_wrapper(ALIGNED_RGB_H, ALIGNED_RGB_W, CV_8UC3, ptr_out_rgb);
            crop_roi.copyTo(out_mat_wrapper);

            // --- D. 处理 Voxel (使用预计算索引) ---
            // 从 vector 直接获取 Start/End Index，无需搜索
            size_t start_idx = m_frameEventIndices[global_frame_idx].first;
            size_t end_idx = m_frameEventIndices[global_frame_idx].second;

            // 获取 Trigger 时间用于归一化
            uint64_t t_trig_start = m_triggers[global_frame_idx].t;
            uint64_t t_trig_end = m_triggers[global_frame_idx + 1].t;

            runVoxelization(start_idx, end_idx, ptr_out_vox, t_trig_start, t_trig_end);
        }

        // 4. 批量写入 HDF5 (IO)
        // Write RGB
        hsize_t write_offset_rgb[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t write_count_rgb[4] = { (hsize_t)current_chunk_size, (hsize_t)ALIGNED_RGB_H, (hsize_t)ALIGNED_RGB_W, 3 };
        H5::DataSpace fspace_rgb = m_rgbOutputDataset.getSpace();
        fspace_rgb.selectHyperslab(H5S_SELECT_SET, write_count_rgb, write_offset_rgb);
        H5::DataSpace mspace_rgb(4, write_count_rgb, NULL);
        m_rgbOutputDataset.write(chunk_out_rgb.data(), H5::PredType::NATIVE_UINT8, mspace_rgb, fspace_rgb);

        // Write Voxels
        hsize_t write_offset_vox[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t write_count_vox[4] = { (hsize_t)current_chunk_size, (hsize_t)VOXEL_BINS, (hsize_t)VOXEL_H, (hsize_t)VOXEL_W };
        H5::DataSpace fspace_vox = m_voxelOutputDataset.getSpace();
        fspace_vox.selectHyperslab(H5S_SELECT_SET, write_count_vox, write_offset_vox);
        H5::DataSpace mspace_vox(4, write_count_vox, NULL);
        m_voxelOutputDataset.write(chunk_out_voxels.data(), H5::PredType::NATIVE_FLOAT, mspace_vox, fspace_vox);

        // 进度通知
        emit progress(QString("Processed %1 / %2 frames...").arg(chunk_start + current_chunk_size).arg(m_numFrames));
    }

    return true;
}

// =========================================================
// 算法: 体素化 (索引遍历优化)
// =========================================================
void DataProcessor::runVoxelization(size_t start_idx, size_t end_idx, float* out_voxel_ptr, uint64_t t_trigger_start, uint64_t t_trigger_end)
{
    if (start_idx >= end_idx) return;

    // 1. 时间归一化参数
    double deltaT = (double)(t_trigger_end - t_trigger_start);
    if (deltaT <= 0) deltaT = 1.0;
    double time_norm_factor = (double)(VOXEL_BINS - 1) / deltaT;

    // 单帧像素数
    int frame_pixel_count = VOXEL_H * VOXEL_W;

    // 2. 线性遍历事件 (无搜索，极快)
    for (size_t i = start_idx; i < end_idx; ++i) {
        const Event& ev = m_events[i];

        // 空间过滤 (Crop)
        if (ev.x < (uint32_t)VOXEL_CROP_X_MIN) continue;
        int x = (int)ev.x - VOXEL_CROP_X_MIN;
        int y = (int)ev.y;

        // 边界检查 (防止越界写内存)
        if (x >= VOXEL_W || y >= VOXEL_H || x < 0 || y < 0) continue;

        float polarity = ev.p ? 1.0f : -1.0f;

        // 时间归一化
        // 必须减去当前帧的 Trigger 起始时间，使 t_norm 落在 [0, VOXEL_BINS-1] 附近
        double t_norm = (double)(ev.t - t_trigger_start) * time_norm_factor;

        int t_idx = (int)std::floor(t_norm);
        float t_weight_right = (float)(t_norm - t_idx);
        float t_weight_left = 1.0f - t_weight_right;

        int spatial_idx = y * VOXEL_W + x; // 2D index

        // 双线性插值写入
        if (t_idx >= 0 && t_idx < VOXEL_BINS) {
            out_voxel_ptr[t_idx * frame_pixel_count + spatial_idx] += polarity * t_weight_left;
        }
        if (t_idx + 1 >= 0 && t_idx + 1 < VOXEL_BINS) {
            out_voxel_ptr[(t_idx + 1) * frame_pixel_count + spatial_idx] += polarity * t_weight_right;
        }
    }

    // 3. 归一化 (Mean-Std Normalization)
    int total_size = VOXEL_BINS * frame_pixel_count;

    double sum = 0.0, sum_sq = 0.0;
    int num_nonzeros = 0;

    // 第一次遍历：统计
    for (int i = 0; i < total_size; ++i) {
        float val = out_voxel_ptr[i];
        if (val != 0.0f) {
            sum += val;
            sum_sq += (val * val);
            num_nonzeros++;
        }
    }

    // 第二次遍历：标准化
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