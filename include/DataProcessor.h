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
    bool loadFromRaw();        // 步骤1: 亿级事件加载与内存预估
    bool createOutputH5();     // 步骤2: 初始化 HDF5
    bool processFramesChunked(); // 步骤3: 分块并行处理 (核心优化)

    // --- 核心算法 ---
    // 使用原始指针进行体素化，避免 vector 构造开销
    void runVoxelization(uint64_t t_start, uint64_t t_end, float* out_voxel_ptr);

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

    int m_numFrames;

    // --- 参数配置 ---
    const int INPUT_RGB_H = 720;
    const int INPUT_RGB_W = 1280;

    const int ALIGNED_RGB_H = 720;
    const int ALIGNED_RGB_W = 1000;

    const int VOXEL_BINS = 5;
    const int VOXEL_H = 720;
    const int VOXEL_W = 1000;
    const int VOXEL_CROP_X_MIN = 280;

    // 分块大小 (根据内存调整，100帧约需 1.5GB 临时内存，非常安全)
    const int CHUNK_SIZE = 100;
};

#endif // DATAPROCESSOR_H