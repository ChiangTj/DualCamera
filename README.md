好的，这是一份为你这个项目量身定制的 README.md。我根据我们之前的对话，特别是你的 `CMakeLists.txt`、代码实现和我们解决的线程问题，总结了项目的核心思路和依赖。

你只需将以下内容复制到一个名为 `README.md` 的文件中，并将其保存在你项目的根目录（`F:\DualCamera\`）下即可。

---

# DualCamera: 工业与事件相机同步采集系统

这是一个基于 C++ 和 Qt 的高性能数据采集软件，用于同步控制、显示和录制来自 **海康（Hikvision）工业相机（RGB）** 和 **Prophesee（Metavision）事件相机（DVS）** 的数据。

项目通过外部信号发生器（例如 `Uno.cpp` 控制的 Arduino）确保两台相机的数据采集在时间上严格同步。

## 📸 主要功能

* **双路实时预览**: 在 Qt GUI 中同时显示 RGB 图像和 DVS 事件累积图像。
* **同步数据采集**: 通过外部触发，确保两种数据流的时间戳一致。
* **高性能数据存储**:
    * RGB 图像数据高效存为 **HDF5** (`.h5`) 文件。
    * DVS 事件数据原生保存为 **RAW** (`.raw`) 文件。
* **多线程架构**: 采集、处理、显示和保存均在独立线程中运行，确保 GUI 实时响应不卡顿。

## 🛠️ 核心依赖 (编译时)

本项目使用 CMake 构建。编译前，你必须在系统中安装以下依赖，并确保 CMake 能够找到它们：

* **构建系统**: **CMake 3.10+**
* **C++ 编译器**: 支持 **C++17** (例如 Visual Studio 2017/2022)
* **GUI 框架**: **Qt 5** (Core, Gui, Widgets)
* **RGB 相机 SDK**: **海康 MVS SDK**
* **DVS 相机 SDK**: **Prophesee Metavision SDK** (core, driver, ui)
* **图像处理**: **OpenCV**
* **数据存储**: **HDF5** (C 和 CXX 库)
* **并行**: **OpenMP** (可选，但已在 CMake 中配置)

## 💡 实现思路 (多线程架构)

本项目的核心是其多线程架构，旨在最大限度地提高性能并防止 GUI 线程（主线程）被任何I/O操作（相机采集、磁盘写入）阻塞。

### 1. GUI 主线程 (`Gui.cpp`)

* **核心原则**: GUI 线程**永远不**执行耗时操作或等待。
* **实现**:
    * 使用两个 `QTimer` (例如，每 33ms 触发一次) 来定期刷新 RGB 和 DVS 的显示。
    * `QTimer` 的槽函数 (`updateRgbDisplaySlot`, `updateDvsDisplaySlot`) 运行在 GUI 线程中。
    * 这些槽函数调用 `rgb.getLatestFrame()` 和 `dvs.getFrame()` 来获取*已经处理好*的最新图像，并将其设置到 `QLabel` 上。
    * 由于 `getFrame()` 被设计为**非阻塞**或**极快**的，GUI 始终保持流畅。

### 2. RGB 相机 (`RGB.cpp`) - 三级线程模型

RGB 数据流需要采集、格式转换、HDF5 写入和 GUI 显示，因此使用了更复杂的生产者-消费者模型：

1.  **线程 A (MVS SDK 回调)**:
    * **任务**: 仅从相机获取原始数据。
    * **操作**: `imageCallback` 将原始数据指针和元信息推入一个线程安全队列 `image_queue`。此回调非常轻量，立即返回。

2.  **线程 B (C++ 线程池)**:
    * **任务**: 并行处理原始数据（最耗CPU的操作）。
    * **操作**: 线程池中的工作线程从 `image_queue` 中取出数据，执行 `MV_CC_ConvertPixelType`（例如转为 BGR），然后将**处理后的 `cv::Mat`** 推入两个队列：
        1.  `display_stack`: 一个（有锁的）堆栈，只保存最新一帧，供 GUI 预览。
        2.  `hdf5_write_queue`: 一个（无界的）队列，供 HDF5 线程写入磁盘。

3.  **线程 C (HDF5 写入专用线程)**:
    * **任务**: 独立地将数据写入磁盘。
    * **操作**: `hdf5WriteLoop` 循环地从 `hdf5_write_queue` 中取出 BGR 帧，并将其写入 HDF5 文件。这有效地将缓慢的磁盘 I/O 与高速的相机采集完全隔离。

### 3. DVS 相机 (`DVS.cpp`) - 高效 `try_lock` 模型

DVS 相机的回调（`cd_frame_generator`）可能比 GUI 刷新率（30 FPS）快得多（例如 100+ FPS）。为避免 CPU 浪费，使用了 `try_lock` 优化：

1.  **线程 A (Metavision SDK 回调)**:
    * **任务**: 高频生成用于预览的 `cv::Mat`。
    * **操作 (写入方)**: 在 lambda 回调中：
        * 使用 `m_frame_mutex.try_lock()` 尝试锁定。
        * **如果锁定成功**: (意味着 GUI 当前没有在读取) 执行 `m_latest_frame = frame.clone()` 来更新最新帧。
        * **如果锁定失败**: (意味着 GUI 正在读取) **什么也不做**，立即放弃这一帧。这避免了 DVS 线程被 GUI 阻塞，并跳过了不必要的（将被覆盖的）`clone()` 操作，极大地节省了 CPU。

2.  **线程 B (GUI 主线程)**:
    * **任务**: 低频获取用于预览的 `cv::Mat`。
    * **操作 (读取方)**: `getFrame()` 函数：
        * 使用 `std::lock_guard` (阻塞锁) 来锁定 `m_frame_mutex`。
        * 安全地 `clone` `m_latest_frame` 并返回。
        * 由于 DVS 回调使用的是 `try_lock`，GUI 线程的这种短暂阻塞是安全的，并且总能获取到一帧完整的图像。
