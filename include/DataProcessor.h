#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H

#include <QObject>
#include <QString>
#include <opencv2/opencv.hpp>
#include <H5Cpp.h>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <QThread>
// Metavision SDK
#include <metavision/sdk/driver/camera.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event_ext_trigger.h>

// 内部结构体
struct Event {
    uint64_t t;
    uint32_t x;
    uint32_t y;
    bool p;
};

struct Trigger {
    uint64_t t;
    short id;
    bool p;
};

class DataProcessor : public QObject
{
    Q_OBJECT

public:
    explicit DataProcessor(const std::string& segmentPath,
        const cv::Mat& homographyMatrix,
        QObject* parent = nullptr);
    ~DataProcessor();

public slots:
    void process();

signals:
    void progress(const QString& message);
    void finished(bool success);

private:
    // --- 步骤函数 ---
    bool loadFromRaw();
    bool createOutputH5();
    bool processFramesChunked();

    // --- 核心算法 (优化版接口) ---
    // 移除 t_start/t_end 查找，改为直接传入 vector 下标范围
    // 同时也传入触发器时间用于归一化计算
    void runVoxelization(size_t start_idx, size_t end_idx, float* out_voxel_ptr, uint64_t t_trigger_start, uint64_t t_trigger_end);

    // --- 成员变量 ---
    std::string m_segmentPath;
    std::string m_segmentName;
    cv::Mat m_homo;

    // HDF5
    std::unique_ptr<H5::H5File> m_outputFile;
    H5::DataSet m_rgbOutputDataset;
    H5::DataSet m_voxelOutputDataset;

    // 内存数据
    std::vector<Event> m_events;
    std::vector<Trigger> m_triggers;

    // [新增] CPU优化关键：预计算每帧对应的事件索引范围 [start, end)
    std::vector<std::pair<size_t, size_t>> m_frameEventIndices;

    int m_numFrames;

    // --- [修改] 参数配置 (适配 500W 相机) ---
    // 假设 500W 相机分辨率约为 2592 x 1944
    const int INPUT_RGB_W = 2592;
    const int INPUT_RGB_H = 1944;

    // 输出对齐尺寸 (保持 720p 或根据需要调整)
    const int ALIGNED_RGB_H = 720;
    const int ALIGNED_RGB_W = 1000;

    const int VOXEL_BINS = 5;
    const int VOXEL_H = 720;
    const int VOXEL_W = 1000;
    const int VOXEL_CROP_X_MIN = 280; // 注意：如果是 500W 分辨率，这个裁剪值可能需要调整！

    // [修改] 分块大小
    // 500W RGB (2592*1944*3) ≈ 15MB/帧
    // 50帧 ≈ 750MB Raw RGB + 750MB Processed + Voxels
    // 内存峰值控制在 2GB 以内，非常安全
    const int CHUNK_SIZE = 50;
};

#endif // DATAPROCESSOR_H