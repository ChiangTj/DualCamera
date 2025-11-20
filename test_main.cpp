#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <opencv2/opencv.hpp>

// 包含你的头文件 (请根据实际目录结构调整路径)
#include "../include/DataProcessor.h"

int main(int argc, char* argv[])
{
    // 使用 QCoreApplication 而不是 QApplication，因为我们不需要 GUI 界面
    QCoreApplication app(argc, argv);

    qInfo() << "========================================";
    qInfo() << "   DataProcessor Standalone Test Tool   ";
    qInfo() << "========================================";

    // ---------------------------------------------------------
    // 1. 配置区：请在这里修改你的测试文件路径
    // ---------------------------------------------------------
    // 文件夹路径：该文件夹下必须包含 .raw 文件和 rgb_data.h5 文件
    std::string segment_path = "G:/test/test6";

    // 单应性矩阵路径
    std::string homography_path = "./homography.xml";
    // ---------------------------------------------------------

    // 2. 准备单应性矩阵 (Homography)
    // 为了测试可行性，如果找不到 xml 文件，我们就伪造一个单位矩阵，防止程序崩溃
    cv::Mat H;
    if (QFile::exists(QString::fromStdString(homography_path))) {
        cv::FileStorage fs(homography_path, cv::FileStorage::READ);
        if (fs.isOpened()) {
            fs["H"] >> H;
            qInfo() << "[Init] Loaded Homography Matrix from XML.";
        }
    }

    if (H.empty()) {
        qWarning() << "[Init] Homography file not found or invalid!";
        qWarning() << "[Init] Using Identity Matrix (No Transformation) for testing purposes.";
        H = cv::Mat::eye(3, 3, CV_64F);
    }

    // 3. 实例化 DataProcessor
    // 注意：这里使用了我们之前优化过的构造函数（支持 500W 硬编码优化）
    // 如果你后来改回了带 config 的版本，请在这里传入 config 路径
    DataProcessor processor(segment_path, H);

    // 4. 连接信号与槽 (用于在控制台看进度)

    // 打印进度信息
    QObject::connect(&processor, &DataProcessor::progress, [](const QString& message) {
        qInfo() << "[Progress]" << message;
        });

    // 处理完成后的回调
    QObject::connect(&processor, &DataProcessor::finished, [&](bool success) {
        if (success) {
            qInfo() << "\n[Result] SUCCESS! Processing completed.";
            qInfo() << "[Result] output saved to: " << QString::fromStdString(segment_path) << "/processed_data.h5";
            app.quit(); // 正常退出程序
        }
        else {
            qCritical() << "\n[Result] FAILED! Processing encountered an error.";
            app.exit(1); // 错误退出
        }
        });

    // 5. 启动处理
    // 使用 QTimer::singleShot(0) 确保在进入事件循环(app.exec)后立即触发 process
    // 这样可以让信号/槽机制正常工作
    QTimer::singleShot(100, [&processor]() {
        qInfo() << "[System] Starting processing logic...";
        processor.process(); // 直接调用处理函数
        });

    // 6. 进入 Qt 事件循环
    return app.exec();
}