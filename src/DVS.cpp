#include "DVS.h"

DVS::DVS() {
    cam = Metavision::Camera::from_first_available();
    cam.get_device().get_facility<Metavision::I_TriggerIn>()->enable(Metavision::I_TriggerIn::Channel::Main);

    camera_width = cam.geometry().width();
    camera_height = cam.geometry().height();

    acc = 20000;
    fps = 50;

    frame_gen = new Metavision::PeriodicFrameGenerationAlgorithm(camera_width, camera_height, acc, fps);

    cd_frame_generator = new Metavision::CDFrameGenerator(camera_width, camera_height);
    cd_frame_generator->set_display_accumulation_time_us(30000);
    cd_frame_generator->start(30, [this](const Metavision::timestamp&, const cv::Mat& frame) {
        if (m_frame_mutex.try_lock()) {
            m_latest_frame = frame.clone();
            m_frame_mutex.unlock();
        }
    });

    cam.cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        frame_gen->process_events(begin, end);
        cd_frame_generator->add_events(begin, end);

        std::lock_guard<std::mutex> lock(m_realtime_mutex);
        if (!m_has_window_start) {
            return;
        }

        const ptrdiff_t batchSize = end - begin;
        if (batchSize > 0) {
            m_current_window_events.reserve(m_current_window_events.size() + static_cast<size_t>(batchSize));
        }

        for (const auto* ev = begin; ev != end; ++ev) {
            DvsEvent event;
            event.t = static_cast<uint64_t>(ev->t);
            event.x = static_cast<uint32_t>(ev->x);
            event.y = static_cast<uint32_t>(ev->y);
            event.p = static_cast<bool>(ev->p);
            m_current_window_events.push_back(event);
        }
    });

    cam.ext_trigger().add_callback([this](const Metavision::EventExtTrigger* begin, const Metavision::EventExtTrigger* end) {
        std::lock_guard<std::mutex> lock(m_realtime_mutex);
        for (const auto* ev = begin; ev != end; ++ev) {
            if (ev->p != 0) {
                continue;
            }

            const uint64_t timestamp = static_cast<uint64_t>(ev->t);
            if (m_has_window_start) {
                DvsEventWindow window;
                window.sequence = m_next_window_sequence++;
                window.startTimestamp = m_window_start_timestamp;
                window.endTimestamp = timestamp;
                window.events = std::move(m_current_window_events);

                if (m_completed_windows.size() >= m_max_completed_windows) {
                    m_completed_windows.pop_front();
                }
                m_completed_windows.emplace_back(std::move(window));
            }

            m_window_start_timestamp = timestamp;
            m_current_window_events.clear();
            m_has_window_start = true;
        }
    });
}

DVS::~DVS() {
    if (cam.is_running()) {
        cam.stop();
    }
    delete frame_gen;
    delete cd_frame_generator;
}

cv::Mat DVS::getFrame() {
    cv::Mat output;
    {
        std::lock_guard<std::mutex> lock(m_frame_mutex);
        if (!m_latest_frame.empty()) {
            output = m_latest_frame.clone();
        }
    }
    return output;
}

bool DVS::tryPopNextRealtimeWindow(DvsEventWindow& window) {
    std::lock_guard<std::mutex> lock(m_realtime_mutex);
    if (m_completed_windows.empty()) {
        return false;
    }

    window = std::move(m_completed_windows.front());
    m_completed_windows.pop_front();
    return true;
}

void DVS::start(const std::string& folder_path, const std::string& file_prefix) {
    std::string full_path = folder_path + "/" + file_prefix + ".raw";
    save_folder = full_path;

    {
        std::lock_guard<std::mutex> lock(m_realtime_mutex);
        m_current_window_events.clear();
        m_completed_windows.clear();
        m_window_start_timestamp = 0;
        m_next_window_sequence = 0;
        m_has_window_start = false;
    }

    try {
        cam.start();
        cam.start_recording(save_folder);
        printf("[DVS] Recording to: %s\n", save_folder.c_str());
    }
    catch (const std::exception& e) {
        printf("[DVS Error] Failed to start recording: %s\n", e.what());
    }
}

void DVS::stop() {
    cam.stop();
}

void DVS::stopRecord() {
    cam.stop_recording();
    cam.stop();

    std::lock_guard<std::mutex> lock(m_realtime_mutex);
    m_current_window_events.clear();
    m_completed_windows.clear();
    m_window_start_timestamp = 0;
    m_next_window_sequence = 0;
    m_has_window_start = false;
}
