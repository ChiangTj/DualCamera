#ifndef REALTIMETYPES_H
#define REALTIMETYPES_H

#include <cstdint>
#include <vector>

#include <opencv2/opencv.hpp>

struct DvsEvent {
    uint64_t t = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    bool p = false;
};

struct DvsEventWindow {
    int sequence = -1;
    uint64_t startTimestamp = 0;
    uint64_t endTimestamp = 0;
    std::vector<DvsEvent> events;
};

struct RealtimeFrameInput {
    unsigned int frameNumber = 0;
    cv::Mat rgbFrame;
    DvsEventWindow eventWindow;
};

#endif // REALTIMETYPES_H
