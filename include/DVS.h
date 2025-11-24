#ifndef DVS_H
#define DVS_H
#include <metavision/sdk/driver/camera.h>
#include <metavision/sdk/driver/ext_trigger.h>
#include <metavision/hal/facilities/i_trigger_in.h>
#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
#include "DataQueue.h"
#include <opencv2/opencv.hpp>
#include <metavision/sdk/core/utils/cd_frame_generator.h>

class DVS {
private:
	Metavision::Camera cam;
	std::uint32_t acc;
	double fps;
	Metavision::PeriodicFrameGenerationAlgorithm  * frame_gen;
	Metavision::CDFrameGenerator* cd_frame_generator;
	//Metavision::ExtTrigger& ext_trigger;
	int camera_width;
	int camera_height;
	std::string save_folder;
	DataQueue<std::pair<const Metavision::EventCD* , const Metavision::EventCD*> > raw_queue;
	// +++ 添加这两个成员来替换 cv::Mat 队列 +++
	std::mutex m_frame_mutex;   // 用于保护 m_latest_frame 的互斥锁
	cv::Mat m_latest_frame;     // 用于存储GUI要读取的最新帧
public:
	DVS();
	~DVS();
	void stopRecord();
	void start(const std::string& folder_path, const std::string& file_prefix);
	void stop();
	//void decode();
	cv::Mat getFrame();

};

#endif // !DVS_H
