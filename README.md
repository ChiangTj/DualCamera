# DualCamera: 同步采集与 Python 处理演示系统

这是一个基于 C++ 和 Qt 的高性能数据采集前端，用于同步控制**海康威视（Hikvision）工业相机（RGB）**和**Prophesee（Metavision）事件相机（DVS）**的数据流。

系统通过外部信号发生器（`Uno.cpp`）确保数据同步，并将采集到的数据段（Segment）交给 Python 后端进行复杂的离线处理（包括对齐、事件处理和深度学习去模糊）。

## 📸 核心架构：录制-处理-回放 (三分离)

为解决 C++ 难以实现复杂算法（如深度学习）和 Python 难以进行高性能硬件I/O的问题，本系统采用混合架构。C++ 负责它最擅长的**实时硬件控制**，Python 负责它最擅长的**数据科学与模型推理**。

1.  **录制 (Record Mode)**:
    * **C++ (Qt GUI)** 负责。
    * 用户点击 `[Start Recording]`。
    * `RGB.cpp` 和 `DVS.cpp` 并行启动，将同步的 `Segment_N` 数据高速写入磁盘 (`.h5` 和 `.raw`)。
    * `view_RGB` 窗口显示实时预览。
    * 用户点击 `[Stop Recording]`，C++ 完成文件写入。

2.  **处理 (Process Mode)**:
    * **C++ (Qt GUI)** 启动，**Python (Script)** 执行。
    * 用户点击 `[Process Last Segment]`。
    * C++ GUI 使用 `QProcess` 启动一个外部的 `master_process.py` 脚本。
    * `master_process.py` 负责执行您所有的 Python 脚本逻辑：格式转换、对齐、裁剪、并运行**深度学习去模糊模型**。
    * Python 将最终的清晰图像保存到 `deblurred/` 文件夹。

3.  **回放 (Playback Mode)**:
    * **C++ (Qt GUI)** 负责。
    * Python 进程结束后，C++ GUI 会自动加载*原始*视频（来自 `rgb_crop/`) 和*已处理*视频（来自 `deblurred/`）。
    * GUI 使用 `QTimer` 在两个 `QLabel` 中**同步循环播放**对比视频。

## 🛠️ 依赖关系

### C++ 编译时依赖 (`CMakeLists.txt`)
* **构建系统**: **CMake 3.10+**
* **C++ 编译器**: 支持 **C++17**
* **GUI 框架**: **Qt 5** (Core, Gui, Widgets)
* **RGB 相机 SDK**: **海康 MVS SDK**
* **DVS 相机 SDK**: **Prophesee Metavision SDK** (core, driver, ui)
* **图像处理**: **OpenCV**
* **数据存储**: **HDF5** (C 和 CXX 库)

### Python 运行时依赖 (Conda 环境)
* **Python 3.8+**
* **`h5py`**: 用于 HDF5 I/O
* **`numpy`**: 用于所有数据处理
* **`opencv-python`**: 用于图像 I/O 和处理
* **`metavision_sdk`**: (Python 版) 用于读取 `.raw`
* **`torch` / `onnxruntime`**: (或您选择的) 用于运行深度学习模型

## 💡 C++ 模块实现细节

### 1. GUI 主线程 (`Gui.cpp`) - 状态机
* **核心**: `Gui.cpp` 被重构为一个状态机 (`AppState`)，管理 `Idle`, `Recording`, `Processing`, `Playback` 状态。
* **录制**: `onRecordButtonClicked()` 调用 `rgb.startCapture()` 和 `dvs.start()`。一个 `QTimer` (`m_livePreviewTimer`) 定期从 `rgb.getLatestFrame()` 获取图像用于实时预览。
* **处理**: `onProcessButtonClicked()` 使用 `QProcess` 启动 Python 脚本。它**不**阻塞 GUI，而是通过 `finished` 信号异步接收 Python 的完成通知。
* **回放**: `onPythonFinished()` 触发 `setupPlayback()`，该函数将 HDF5 和 PNG 图像**一次性加载**到两个 `std::vector<cv::Mat>`。另一个 `QTimer` (`m_playbackTimer`) 启动，以 30fps 的速度同步播放这两个向量。

### 2. RGB 相机 (`RGB.cpp`) - 高性能 HDF5 录制器
RGB 数据流使用三级线程模型来保证性能：

1.  **线程 A (MVS SDK 回调)**:
    * **任务**: 采集。`imageCallback` 仅将原始数据和元信息（`frame_number`） 推入 `image_queue`。
2.  **线程 B (C++ 线程池)**:
    * **任务**: 转换。工作线程从 `image_queue` 取数据，转为 `cv::Mat` (BGR)，并推入 `display_stack` (用于GUI预览) 和 `hdf5_write_queue` (用于保存)。
3.  **线程 C (HDF5 写入线程)**:
    * **任务**: 保存。`hdf5WriteLoop` 是**唯一**写入 HDF5 的线程，它从 `hdf5_write_queue` 取数据，将 `cv::Mat` 和 `frame_number`（**关键的同步键**）追加到 `.h5` 文件中。

### 3. DVS 相机 (`DVS.cpp`) - 原生 RAW 录制器
DVS 模块的职责被简化，以实现最高性能：
* **录制 (`startRecord`)**: 直接调用 Metavision SDK 高度优化的 `cam.start_recording(save_folder)`。这比任何手动的 HDF5 写入都要快。
* **预览 (已移除)**: `getFrame()` 和 `try_lock` 逻辑保留在代码中，但不再被 `Gui.cpp` 调用，以满足 UI 简化需求。

### 4. 信号发生器 (`Uno.cpp`)
* **任务**: 硬件同步。
* **操作**: 一个简单的串口封装器。`start()` 发送 'a' 到 `COM3`，`stop()` 发送 'q'，启动/停止外部触发方波。

## 🚀 如何运行

### 1. 编译 C++
1.  确保所有 C++ 依赖（Qt, MVS, Metavision, HDF5, OpenCV）均已安装。
2.  **（关键修复）** 确保 HDF5 库已正确链接。在 `CMakeLists.txt` 中，使用 `find_package(HDF5 COMPONENTS C CXX REQUIRED)` 并通过 `HDF5_DIR` 变量（例如指向 Anaconda 库）或修改 Prophesee 的 HDF5 配置文件来解决链接器错误。
3.  **（关键修复）** 在 `RGB.cpp` 中，**删除或注释掉** `stopCapture()` 函数中的 `hdf5_write_queue.stopWait();` 这一行，因为它在 `DataQueue.h` 中不存在，会导致编译失败。
4.  使用 CMake 和 Visual Studio 编译项目。

### 2. 准备 Python
1.  安装 Anaconda 或 Miniconda。
2.  创建 Conda 环境: `conda create -n deblur_env python=3.9`
3.  `conda activate deblur_env`
4.  安装所有 Python 依赖: `pip install numpy h5py opencv-python ...` (以及 PyTorch 和 Metavision SDK for Python)。
5.  将您所有的 `.py` 脚本 组合成一个 `master_process.py`。
6.  将 `master_process.py` 放置在 C++ `.exe` 文件旁边。

### 3. 运行演示
1.  **打开 Anaconda Prompt**（或已配置的终端）。
2.  **激活环境**: `conda activate deblur_env`
3.  **运行 GUI**: (从**同一个**终端) `cd path\to\your\build\Release`，然后运行 `dualcamera.exe`。
4.  **操作**:
    * 在 GUI 中输入数据集名称 (例如 "Test_01")。
    * 点击 `[Start Recording]`。
    * ... 录制数据 ...
    * 点击 `[Stop Recording]`。
    * 点击 `[Process Last Segment]` (此时 GUI 冻结，Python 在后台运行)。
    * ... 等待 Python 完成 ...
    * GUI 自动切换到回放模式，开始同步播放对比视频。