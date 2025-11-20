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
#include <utility> // for std::pair
#include <chrono>  // [新增] 计时支持
#include <map>     // [新增] 统计存储
#include <numeric> // [新增] 统计计算
#include <algorithm>

// Metavision SDK
#include <metavision/sdk/driver/camera.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event_ext_trigger.h>

// 内部结构体：用于存储转换后的事件
struct Event {
    uint64_t t;
    uint32_t x;
    uint32_t y;
    bool p;
};

// 内部结构体：用于存储触发信号
struct Trigger {
    uint64_t t;
    short id;
    bool p;
};

// [新增] 简单的线程安全性能分析器
class SimpleProfiler {
public:
    // 添加一条记录 (单位: ms)
    void addRecord(const std::string& name, double ms) {
        std::lock_guard<std::mutex> lock(mtx);
        records[name].push_back(ms);
    }

    // 生成统计报告
    QString getReport() {
        QString report = "\n=== Performance Profile (ms) ===\n";
        report += QString("%1").arg("Name", -20) + QString("%1").arg("Avg", -10) + QString("%1").arg("P50", -10) + QString("%1").arg("P95", -10) + QString("%1").arg("P99", -10) + QString("%1").arg("Count", -10) + "\n";
        report += QString("-").repeated(75) + "\n";

        for (auto& kv : records) {
            std::vector<double>& v = kv.second;
            if (v.empty()) continue;

            // 排序以计算分位数
            std::sort(v.begin(), v.end());

            double sum = std::accumulate(v.begin(), v.end(), 0.0);
            double avg = sum / v.size();
            double p50 = v[v.size() * 0.50];
            double p95 = v[(std::min)((size_t)(v.size() * 0.95), v.size() - 1)];
            double p99 = v[(std::min)((size_t)(v.size() * 0.99), v.size() - 1)];

            report += QString("%1").arg(QString::fromStdString(kv.first), -20)
                + QString::number(avg, 'f', 2).leftJustified(10)
                + QString::number(p50, 'f', 2).leftJustified(10)
                + QString::number(p95, 'f', 2).leftJustified(10)
                + QString::number(p99, 'f', 2).leftJustified(10)
                + QString::number(v.size()).leftJustified(10) + "\n";
        }
        report += "================================\n";
        return report;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        records.clear();
    }

private:
    std::map<std::string, std::vector<double>> records;
    std::mutex mtx;
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
    // 主处理入口
    void process();

signals:
    // 用于向 GUI 报告进度
    void progress(const QString& message);
    // 处理完成信号
    void finished(bool success);

private:
    // --- 步骤函数 ---
    bool loadFromRaw();          // 步骤1: 加载 RAW 数据并进行预处理
    bool createOutputH5();       // 步骤3: 初始化 HDF5 输出文件结构
    bool processFramesChunked(); // 步骤4: 分块并行处理 (核心优化逻辑)

    // --- 核心算法 ---
    // [修改] 接收预计算的索引范围和 Trigger 时间范围
    void runVoxelization(size_t start_idx, size_t end_idx, float* out_voxel_ptr, uint64_t t_trigger_start, uint64_t t_trigger_end);

    // --- 成员变量 ---
    std::string m_segmentPath;
    std::string m_segmentName;
    cv::Mat m_homo;

    // HDF5 相关
    std::unique_ptr<H5::H5File> m_outputFile;
    H5::DataSet m_rgbOutputDataset;
    H5::DataSet m_voxelOutputDataset;

    // 内存数据容器
    std::vector<Event> m_events;
    std::vector<Trigger> m_triggers;

    // [新增] 预计算索引，用于 CPU 优化
    std::vector<std::pair<size_t, size_t>> m_frameEventIndices;

    int m_numFrames;

    // [新增] 性能分析器实例
    SimpleProfiler m_profiler;

    // --- 参数配置 (已更新为适配 500W 相机) ---
    const int INPUT_RGB_W = 2592;
    const int INPUT_RGB_H = 1944;

    const int ALIGNED_RGB_H = 720;
    const int ALIGNED_RGB_W = 1000;

    const int VOXEL_BINS = 5;
    const int VOXEL_H = 720;
    const int VOXEL_W = 1000;

    const int VOXEL_CROP_X_MIN = 280;

    // 分块大小 (500W 像素下内存压力大，建议设为 50)
    const int CHUNK_SIZE = 50;
};

#endif // DATAPROCESSOR_H