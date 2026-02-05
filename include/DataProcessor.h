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
#include <chrono>  // 璁℃椂鏀寔
#include <map>     // 缁熻瀛樺偍
#include <numeric> // 缁熻璁＄畻
#include <algorithm>

// Metavision SDK
#include <metavision/sdk/driver/camera.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event_ext_trigger.h>

#include "InferenceWorker.h"
#include "RealtimeTypes.h"

// 鍐呴儴缁撴瀯浣擄細鐢ㄤ簬瀛樺偍瑙﹀彂淇″彿
struct Trigger {
    uint64_t t;
    short id;
    bool p;
};

// 绠€鍗曠殑绾跨▼瀹夊叏鎬ц兘鍒嗘瀽鍣?
class SimpleProfiler {
public:
    void addRecord(const std::string& name, double ms) {
        std::lock_guard<std::mutex> lock(mtx);
        records[name].push_back(ms);
    }

    QString getReport() {
        QString report = "\n=== Performance Profile (ms) ===\n";
        report += QString("%1").arg("Name", -20) + QString("%1").arg("Avg", -10) + QString("%1").arg("P50", -10) + QString("%1").arg("P95", -10) + QString("%1").arg("P99", -10) + QString("%1").arg("Count", -10) + "\n";
        report += QString("-").repeated(75) + "\n";

        for (auto& kv : records) {
            std::vector<double>& v = kv.second;
            if (v.empty()) continue;

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
    enum class Mode {
        Batch,
        Realtime
    };

    struct RealtimeOutput {
        cv::Mat alignedRgb;
        std::vector<float> rgbNchw;
        std::vector<float> voxelGrid;
    };

    explicit DataProcessor(const std::string& segmentPath,
        const cv::Mat& homographyMatrix,
        QObject* parent = nullptr);
    ~DataProcessor();

public slots:
public:
    void setMode(Mode mode);
    void setRealtimeOutputConfig(int rgbWidth, int rgbHeight,
        int voxelWidth, int voxelHeight,
        int voxelBins, int voxelCropXMin);
    bool processRealtimeFrame(const cv::Mat& rgbFrame,
        const std::vector<DvsEvent>& events,
        uint64_t t_trigger_start,
        uint64_t t_trigger_end,
        RealtimeOutput& output);

    void runVoxelization(const std::vector<DvsEvent>& events, size_t start_idx, size_t end_idx, float* out_voxel_ptr, uint64_t t_trigger_start, uint64_t t_trigger_end);
    void ensureRealtimeRemap(int outputWidth, int outputHeight, int voxelCropXMin);
    void rgbMatToNchw(const cv::Mat& rgbMat, std::vector<float>& outNchw) const;

    Mode m_mode = Mode::Batch;
    int m_realtimeRgbW = 0;
    int m_realtimeRgbH = 0;
    int m_realtimeVoxelW = 0;
    int m_realtimeVoxelH = 0;
    int m_realtimeVoxelBins = 0;
    int m_realtimeVoxelCropXMin = 0;
    cv::Mat m_realtimeMapX;
    cv::Mat m_realtimeMapY;
    cv::Mat m_realtimeRemapHInv;

    // 涓诲鐞嗗叆鍙?
    void process();

    // [鏂板] 娉ㄥ叆鎺ㄧ悊闃熷垪鐨勬帴鍙?
    void setInferenceQueue(std::shared_ptr<LatestFrameQueue<PreprocessPacket>> queue) {
        m_inferenceQueue = queue;
    }

signals:
    // 鐢ㄤ簬鍚?GUI 鎶ュ憡杩涘害
    void progress(const QString& message);
    // 澶勭悊瀹屾垚淇″彿
    void finished(bool success);

    // [鏂板] 鍙戦€佸鐞嗗ソ鐨勫崟甯у榻?RGB 鍥剧粰 GUI (鐢ㄤ簬鍥炴斁鍜屽乏渚ч瑙?
    void blurryFrameReady(const cv::Mat& frame);

private:
    // --- 姝ラ鍑芥暟 ---
    bool loadFromRaw();          // 姝ラ1: 鍔犺浇 RAW 鏁版嵁骞惰繘琛岄澶勭悊
    bool processFramesChunked(); // 姝ラ3: 鍒嗗潡骞惰澶勭悊 (鎺ㄦ祦鑷?TRT)

    // --- 鏍稿績绠楁硶 ---
    void runVoxelization(size_t start_idx, size_t end_idx, float* out_voxel_ptr, uint64_t t_trigger_start, uint64_t t_trigger_end);

    // --- 鎴愬憳鍙橀噺 ---
    std::string m_segmentPath;
    std::string m_segmentName;
    cv::Mat m_homo;

    // 鍐呭瓨鏁版嵁瀹瑰櫒
    std::vector<DvsEvent> m_events;
    std::vector<Trigger> m_triggers;

    // 棰勮绠楃储寮曪紝鐢ㄤ簬 CPU 浼樺寲
    std::vector<std::pair<size_t, size_t>> m_frameEventIndices;

    int m_numFrames;

    // 鎬ц兘鍒嗘瀽鍣ㄥ疄渚?
    SimpleProfiler m_profiler;

    // [鏂板] 鎺ㄧ悊闃熷垪鎸囬拡
    std::shared_ptr<LatestFrameQueue<PreprocessPacket>> m_inferenceQueue;

    // --- 鍙傛暟閰嶇疆 ---
    const int INPUT_RGB_W = 2592;
    const int INPUT_RGB_H = 1944;

    const int ALIGNED_RGB_H = 720;
    const int ALIGNED_RGB_W = 1000;

    const int VOXEL_BINS = 6;
    const int VOXEL_H = 720;
    const int VOXEL_W = 1000;

    const int VOXEL_CROP_X_MIN = 280;

    // 鍒嗗潡澶у皬 (500W 鍍忕礌涓嬪唴瀛樺帇鍔涘ぇ锛屽缓璁涓?50)
    const int CHUNK_SIZE = 50;
};

#endif // DATAPROCESSOR_H
