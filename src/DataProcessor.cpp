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
        // 1. 加载
        emit progress("Step 1/3: Loading RAW data (Optimized)...");
        if (!loadFromRaw()) throw std::runtime_error("Failed to load RAW.");

        // 2. 初始化
        emit progress("Step 2/3: Creating HDF5 output...");
        if (!createOutputH5()) throw std::runtime_error("Failed to create HDF5.");

        // 3. 分块并行计算
        emit progress(QString("Step 3/3: Processing %1 frames (Chunked Parallel)...").arg(m_numFrames));
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
// Step 1: 高性能 RAW 加载 (适应亿级数据)
// =========================================================
bool DataProcessor::loadFromRaw()
{
    std::string raw_path = m_segmentPath + "/" + m_segmentName + ".raw";
    if (!QFile::exists(QString::fromStdString(raw_path))) return false;

    try {
        // --- 1. 智能内存预分配 ---
        // EVT3 事件约 8-16 字节。file_size / 8 是一个安全的上限。
        auto fsize = std::filesystem::file_size(raw_path);
        size_t estimated_events = fsize / 8;
        qInfo() << "Estimated events:" << estimated_events;
        m_events.reserve(estimated_events); // 关键：一次性分配，避免亿级扩容

        // --- 2. 极速读取 ---
        Metavision::Camera cam = Metavision::Camera::from_file(
            raw_path,
            Metavision::FileConfigHints().real_time_playback(false)
        );

        cam.cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
            for (const auto* ev = begin; ev != end; ++ev) {
                m_events.push_back(Event{
                    (uint64_t)ev->t, (uint32_t)ev->x, (uint32_t)(719 - ev->y), (bool)ev->p
                    });
            }
            });

        cam.ext_trigger().add_callback([this](const Metavision::EventExtTrigger* begin, const Metavision::EventExtTrigger* end) {
            for (const auto* ev = begin; ev != end; ++ev) {
                if (ev->p == 0) { // 硬件极性反转修正
                    m_triggers.push_back(Trigger{ (uint64_t)ev->t, ev->id, (bool)ev->p });
                }
            }
            });

        cam.start();
        while (cam.is_running()) QThread::msleep(10);
        cam.stop();

    }
    catch (...) { return false; }

    qInfo() << "Loaded" << m_events.size() << "events.";

    // 确保时间有序
    std::sort(m_events.begin(), m_events.end(), [](const Event& a, const Event& b) {
        return a.t < b.t;
        });

    // --- 3. 帧数校验 (包含 frame_num 检查) ---
    // (此处保留之前的逻辑，读取 rgb_data.h5 获取总帧数并匹配触发器)
    // 为节省篇幅，简略写出
    std::string rgb_h5_path = m_segmentPath + "/rgb_data.h5";
    int rgb_count = 0;
    try {
        H5::H5File f(rgb_h5_path, H5F_ACC_RDONLY);
        H5::DataSet ds = f.openDataSet("rgb/frames");
        hsize_t dims[4];
        ds.getSpace().getSimpleExtentDims(dims, NULL);
        rgb_count = (int)dims[0];
        // (可选：在此处添加 frame_nums 丢帧检查代码)
    }
    catch (...) { return false; }

    if (m_triggers.size() < 2) return false;
    m_numFrames = std::min(rgb_count, (int)m_triggers.size() - 1);

    return (m_numFrames > 0);
}

// =========================================================
// Step 2: 创建输出 (不变)
// =========================================================
bool DataProcessor::createOutputH5()
{
    try {
        std::string out_path = m_segmentPath + "/processed_data.h5";
        m_outputFile = std::make_unique<H5::H5File>(out_path, H5F_ACC_TRUNC);

        hsize_t rgb_dims[4] = { (hsize_t)m_numFrames, (hsize_t)ALIGNED_RGB_H, (hsize_t)ALIGNED_RGB_W, 3 };
        m_rgbOutputDataset = m_outputFile->createDataSet("rgb_aligned", H5::PredType::NATIVE_UINT8, H5::DataSpace(4, rgb_dims));

        hsize_t vox_dims[4] = { (hsize_t)m_numFrames, (hsize_t)VOXEL_BINS, (hsize_t)VOXEL_H, (hsize_t)VOXEL_W };
        m_voxelOutputDataset = m_outputFile->createDataSet("event_voxels", H5::PredType::NATIVE_FLOAT, H5::DataSpace(4, vox_dims));
        return true;
    }
    catch (...) { return false; }
}

// =========================================================
// Step 3: 分块并行处理 (核心优化)
// =========================================================
bool DataProcessor::processFramesChunked()
{
    std::string rgb_h5_path = m_segmentPath + "/rgb_data.h5";
    H5::H5File rgb_inputFile(rgb_h5_path, H5F_ACC_RDONLY);
    H5::DataSet rgb_inputDataset = rgb_inputFile.openDataSet("rgb/frames");

    // 预计算单帧大小 (字节)
    size_t raw_rgb_size = INPUT_RGB_H * INPUT_RGB_W * 3;
    size_t aligned_rgb_size = ALIGNED_RGB_H * ALIGNED_RGB_W * 3;
    size_t voxel_size = VOXEL_BINS * VOXEL_H * VOXEL_W; // floats

    // --- 外层循环：按块处理 (Serial) ---
    for (int chunk_start = 0; chunk_start < m_numFrames; chunk_start += CHUNK_SIZE)
    {
        int current_chunk_size = std::min(CHUNK_SIZE, m_numFrames - chunk_start);

        // 1. 内存池分配 (RAM Buffers)
        // 输入缓冲: 保存一整块原始 RGB
        std::vector<uint8_t> chunk_raw_rgb(current_chunk_size * raw_rgb_size);
        // 输出缓冲: 保存一整块处理后的 RGB 和 Voxels
        std::vector<uint8_t> chunk_out_rgb(current_chunk_size * aligned_rgb_size);
        std::vector<float> chunk_out_voxels(current_chunk_size * voxel_size);

        // 2. 批量读取 RGB (极速 IO，无锁)
        // 一次性读取 current_chunk_size 帧，这比循环读快得多
        hsize_t read_offset[4] = { (hsize_t)chunk_start, 0, 0, 0 };
        hsize_t read_count[4] = { (hsize_t)current_chunk_size, (hsize_t)INPUT_RGB_H, (hsize_t)INPUT_RGB_W, 3 };

        H5::DataSpace file_space = rgb_inputDataset.getSpace();
        file_space.selectHyperslab(H5S_SELECT_SET, read_count, read_offset);
        H5::DataSpace mem_space(4, read_count, NULL);

        rgb_inputDataset.read(chunk_raw_rgb.data(), H5::PredType::NATIVE_UINT8, mem_space, file_space);

        // 3. 块内并行计算 (OpenMP)
#pragma omp parallel for
        for (int i = 0; i < current_chunk_size; ++i)
        {
            int global_frame_idx = chunk_start + i;

            // --- A. 指针算术 (零拷贝) ---
            // 指向输入 RGB
            uint8_t* ptr_raw = chunk_raw_rgb.data() + i * raw_rgb_size;
            // 指向输出 RGB
            uint8_t* ptr_out_rgb = chunk_out_rgb.data() + i * aligned_rgb_size;
            // 指向输出 Voxel
            float* ptr_out_vox = chunk_out_voxels.data() + i * voxel_size;

            // --- B. 内存重置 (memset 优化) ---
            // 替代 vector 构造函数，极快
            std::memset(ptr_out_vox, 0, voxel_size * sizeof(float));

            // --- C. 处理 RGB ---
            // 构造 Mat 包装器 (不分配内存)
            cv::Mat raw_mat(INPUT_RGB_H, INPUT_RGB_W, CV_8UC3, ptr_raw);

            // 线程局部临时变量 (warp 目标)
            // 使用 create 确保内存复用 (虽然在循环内，但 create 会智能处理)
            cv::Mat warped_mat;
            warped_mat.create(INPUT_RGB_H, INPUT_RGB_W, CV_8UC3);

            cv::warpPerspective(raw_mat, warped_mat, m_homo, cv::Size(INPUT_RGB_W, INPUT_RGB_H));

            // 裁剪并复制到输出缓冲
            cv::Mat crop_roi = warped_mat(cv::Rect(VOXEL_CROP_X_MIN, 0, ALIGNED_RGB_W, ALIGNED_RGB_H));
            // 必须使用 memcpy 或 Mat::copyTo，因为 crop_roi 内存不连续，而我们需要连续内存写 H5
            // 这里构建一个指向输出缓冲的 Mat，直接 copyTo
            cv::Mat out_mat_wrapper(ALIGNED_RGB_H, ALIGNED_RGB_W, CV_8UC3, ptr_out_rgb);
            crop_roi.copyTo(out_mat_wrapper);

            // --- D. 处理 Voxel ---
            runVoxelization(m_triggers[global_frame_idx].t, m_triggers[global_frame_idx + 1].t, ptr_out_vox);
        }

        // 4. 批量写入 (极速 IO，无锁)
        // 一次性写入整个块
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

        // 进度更新
        emit progress(QString("Processed %1 / %2 frames...").arg(chunk_start + current_chunk_size).arg(m_numFrames));
    }

    return true;
}

// =========================================================
// 算法: 体素化 (直接操作指针，极致性能)
// =========================================================
void DataProcessor::runVoxelization(uint64_t t_start, uint64_t t_end, float* out_voxel_ptr)
{
    // 1. 二分查找
    Event target; target.t = t_start;
    auto it = std::lower_bound(m_events.begin(), m_events.end(), target,
        [](const Event& a, const Event& b) { return a.t < b.t; });

    if (it == m_events.end() || it->t >= t_end) return;

    // 2. 参数预计算
    double deltaT = (double)(t_end - t_start);
    if (deltaT <= 0) deltaT = 1.0;
    double time_norm_factor = (double)(VOXEL_BINS - 1) / deltaT;
    int frame_pixel_count = VOXEL_H * VOXEL_W; // H*W

    // 3. 累积
    for (; it != m_events.end(); ++it) {
        const Event& ev = *it;
        if (ev.t >= t_end) break;

        if (ev.x < VOXEL_CROP_X_MIN) continue;
        int x = ev.x - VOXEL_CROP_X_MIN;
        int y = ev.y;
        if (x >= VOXEL_W || y >= VOXEL_H) continue;

        float polarity = ev.p ? 1.0f : -1.0f;
        double t_norm = (double)(ev.t - t_start) * time_norm_factor;
        int t_idx = (int)std::floor(t_norm);
        float t_weight_right = (float)(t_norm - t_idx);
        float t_weight_left = 1.0f - t_weight_right;

        int spatial_idx = y * VOXEL_W + x; // 2D index

        // 写入指针: out_voxel_ptr 指向当前帧体素块的起始位置
        // 索引 = bin_idx * (H*W) + spatial_idx
        if (t_idx >= 0 && t_idx < VOXEL_BINS) {
            out_voxel_ptr[t_idx * frame_pixel_count + spatial_idx] += polarity * t_weight_left;
        }
        if (t_idx + 1 >= 0 && t_idx + 1 < VOXEL_BINS) {
            out_voxel_ptr[(t_idx + 1) * frame_pixel_count + spatial_idx] += polarity * t_weight_right;
        }
    }

    // 4. 归一化 (统计非零)
    // 注意：现在我们要遍历整个 voxel buffer (大小为 VOXEL_BINS * H * W)
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