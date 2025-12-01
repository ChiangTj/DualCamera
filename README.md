DualCamera Real-time Deblur System

这是一个高性能的双摄（RGB + 事件相机）实时去模糊演示系统。系统采用 C++ (Qt) 进行前端控制、高速数据采集和高性能预处理，并无缝集成 Python (PyTorch) 进行深度学习推理，最终在 GUI 上实时回放清晰化结果。

?? 系统架构

本系统设计旨在解决 500万像素 @ 60FPS 高吞吐量下的实时处理瓶颈，采用 "采集-预处理-推理" 流水线架构：

1. 高速录制 (Recording Phase)

RGB: 海康威视 (Hikvision) 工业相机，5MP @ 60FPS。使用多线程 + HDF5 直接写入，防止丢帧。

DVS: Prophesee 事件相机。使用 Metavision SDK 录制 RAW 数据。

同步: 通过 Arduino (Uno) 发送硬件触发信号，确保两台相机微秒级同步。

2. 高性能预处理 (Preprocessing Phase - C++)

为应对海量数据，我们在 C++ DataProcessor 模块中实施了深度优化：

查表法 (Lookup Table Remap): 替代 warpPerspective，预计算坐标映射，将 500W 像素到 720p 的对齐与裁剪融合在一步操作中，大幅降低 CPU 负载。

CPU 预索引 (Pre-indexing): 使用滑动窗口算法 ($O(N)$) 预计算每一帧对应的事件时间窗，消除内层循环搜索开销。

OpenMP 并行: 核心算法全并行化处理。

内存复用: 避免频繁内存分配，防止内存碎片化。

3. 深度推理 (Inference Phase - Python)

胶水层: C++ 通过 QProcess 调用 model/run_inference.py。

数据加载: 自定义 H5ImageDataset 直接读取 C++ 生成的 HDF5 文件，零拷贝加载到 PyTorch Tensor。

模型: 基于 BasicSR 框架的去模糊网络 (EFNet 等)，输出清晰图像序列。

??? 硬件要求

RGB 相机: Hikvision MV-CA050-20GC (或兼容 GigE/USB3 相机)

事件相机: Prophesee EVK (VGA 或 HD)

同步板: Arduino Uno (烧录对应触发程序)

计算平台:

CPU: 建议 i7/i9 (多核性能对 OpenMP 至关重要)

RAM: 16GB+ (建议 32GB)

GPU: NVIDIA RTX 3060+ (用于模型推理)

?? 软件依赖

C++ 环境

Visual Studio 2019/2022 (C++17 支持)

CMake 3.10+

Qt 5.15 (MSVC 2019 64-bit)

OpenCV 4.5+

HDF5 (1.10+, C/C++ 库)

Metavision SDK (4.0+)

MVS SDK (Hikvision 官方驱动)

Python 环境 (建议 Conda)

Python 3.8+

PyTorch 1.10+ (CUDA 版本)

h5py, opencv-python, pyyaml, tqdm, basicsr

?? 安装与构建

1. 克隆仓库

git clone [https://github.com/yourusername/DualCamera-Deblur.git](https://github.com/yourusername/DualCamera-Deblur.git)
cd DualCamera-Deblur


2. 配置 Python 环境

conda create -n deblur python=3.9
conda activate deblur
pip install torch torchvision --index-url [https://download.pytorch.org/whl/cu118](https://download.pytorch.org/whl/cu118)
pip install h5py opencv-python pyyaml tqdm scipy
# 安装 BasicSR (如果是本地源码)
cd model
python setup.py develop


3. 编译 C++ 工程

打开 Visual Studio，选择 "打开本地文件夹" -> 选择项目根目录。

确保 CMakeLists.txt 中的库路径指向你本地的安装位置：

MV_LIB_DIR (海康 SDK)

HDF5_DIR

Qt5_DIR

配置 CMake 为 x64-Release (强烈建议 Release 模式以获得完整性能)。

生成并编译。

?? 使用指南

启动程序: 运行编译生成的 dualcamera.exe。

连接设备: 确保两台相机和 Arduino 已连接。

录制:

输入数据集名称 (如 Test01)。

点击 Start Recording。保持相机稳定拍摄运动物体。

点击 Stop Recording。

处理:

点击 Process Last Segment。

观察状态栏：

Step 1: C++ 进行 Remap 和 Voxelization。

Step 2: 自动唤起 Python 进行 AI 推理。

回放:

处理完成后，界面会自动加载模糊原图和去模糊结果。

使用 Play/Pause 按钮或拖动进度条对比效果。

?? 项目结构

DualCamera/
├── CMakeLists.txt          # C++ 构建配置
├── src/                    # C++ 源码 (GUI, RGB, DVS, DataProcessor)
├── include/                # C++ 头文件
├── model/                  # 深度学习模块
│   ├── net_g_xxx.pth       # 预训练权重
│   ├── real.yml            # 模型配置
│   ├── run_inference.py    # [关键] C++ 调用入口
│   ├── basicsr/            # 模型核心库
│   └── ...
└── README.md


?? 注意事项

HDF5 保护: 录制过程中请勿强制关闭控制台或杀进程，否则会导致 HDF5 文件头损坏（0帧问题）。请务必点击 GUI 上的 Stop 按钮。

分辨率: 默认适配 2592 x 2160 输入。如更换相机，请修改 DataProcessor.h 中的 INPUT_RGB_W/H。

单应性矩阵: 程序启动时需要读取 homography.xml。