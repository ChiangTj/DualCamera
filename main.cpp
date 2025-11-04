#include <QApplication>
#include "include/Gui.h"

int main(int argc, char* argv[]) {
	QApplication a(argc, argv);
	GUI w;
	w.setWindowTitle("DualCamera");
	w.show();
	return a.exec();
}

//#include <metavision/sdk/driver/camera.h>
//#include <metavision/sdk/core/algorithms/periodic_frame_generation_algorithm.h>
//#include <metavision/sdk/ui/utils/window.h>
//#include <metavision/sdk/ui/utils/event_loop.h>

//int main(int argc, char* argv[]) {
//
//    auto cam = Metavision::Camera::from_first_available();
//
//    const auto w = cam.geometry().width();
//    const auto h = cam.geometry().height();
//    const std::uint32_t acc = 20000;
//    double fps = 50;
//    auto frame_gen = Metavision::PeriodicFrameGenerationAlgorithm(w, h, acc, fps);
//    // Setup CD frame generator
//    std::mutex cd_frame_generator_mutex;
//    Metavision::CDFrameGenerator cd_frame_generator(w, h);
//    cd_frame_generator.set_display_accumulation_time_us(10000);
//
//    std::mutex cd_frame_mutex;
//    cv::Mat cd_frame;
//    Metavision::timestamp cd_frame_ts{ 0 };
//    cd_frame_generator.start(
//        30, [&cd_frame_mutex, &cd_frame, &cd_frame_ts](const Metavision::timestamp& ts, const cv::Mat& frame) {
//            std::unique_lock<std::mutex> lock(cd_frame_mutex);
//            cd_frame_ts = ts;
//            frame.copyTo(cd_frame);
//        });
//
//
//    //Metavision::Window window("Frames", w, h, Metavision::BaseWindow::RenderMode::BGR);
//
//    frame_gen.set_output_callback([&](Metavision::timestamp, cv::Mat& frame) {
//        //window.show(frame);
//        });
//
//    // Setup camera CD callback to update the frame generator and event rate estimator
//    int cd_events_cb_id =
//        cam.cd().add_callback([&cd_frame_generator_mutex, &cd_frame_generator](
//            const Metavision::EventCD* ev_begin, const Metavision::EventCD* ev_end) {
//                std::unique_lock<std::mutex> lock(cd_frame_generator_mutex);
//                cd_frame_generator.add_events(ev_begin, ev_end);
//            });
//
//
//    cam.start();
//    while (cam.is_running()) {
//        if(!cd_frame.empty())
//            cv::imshow("cd_window_name", cd_frame);
//        cv::waitKey(1);
//    }
//    cam.stop();
//
//    return 0;
//}


