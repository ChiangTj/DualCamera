#ifndef DVS_H
#define DVS_H

#include <metavision/sdk/driver/camera.h>
#include <metavision/sdk/driver/ext_trigger.h>
#include <metavision/hal/facilities/i_trigger_in.h>
#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "RealtimeTypes.h"

class DVS {
private:
    Metavision::Camera cam;
    std::uint32_t acc = 0;
    double fps = 0.0;
    Metavision::PeriodicFrameGenerationAlgorithm* frame_gen = nullptr;
    Metavision::CDFrameGenerator* cd_frame_generator = nullptr;
    int camera_width = 0;
    int camera_height = 0;
    std::string save_folder;

    std::mutex m_frame_mutex;
    cv::Mat m_latest_frame;

    std::mutex m_realtime_mutex;
    std::vector<DvsEvent> m_current_window_events;
    std::deque<DvsEventWindow> m_completed_windows;
    uint64_t m_window_start_timestamp = 0;
    int m_next_window_sequence = 0;
    bool m_has_window_start = false;
    const size_t m_max_completed_windows = 8;

public:
    DVS();
    ~DVS();

    void stopRecord();
    void start(const std::string& folder_path, const std::string& file_prefix);
    void stop();
    cv::Mat getFrame();
    bool tryPopNextRealtimeWindow(DvsEventWindow& window);
};

#endif // DVS_H
