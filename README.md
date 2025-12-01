# DualCamera Real-time Deblur System  
**双摄实时去模糊系统**

这是一个基于 **C++ (Qt)** 与 **Python (PyTorch)** 的混合架构高性能演示系统。系统结合 **RGB 工业相机** 与 **事件相机（DVS）** 的互补特性，在高速运动场景中实现实时图像去模糊。  

前端 C++ 负责微秒级硬件同步、高速数据采集（60FPS @ 500万像素）和高性能预处理；  
后端 Python 负责深度学习模型推理（如 EFNet）；  
最终在 GUI 实现 **录制–处理–回放** 的一站式体验。

---

## ?? 系统架构

为解决高分辨率（2592×1944）与高帧率数据流带来的处理瓶颈，系统采用 **在线采集–流水线处理** 架构：

---

## 1. 高速采集层 (C++ Backend)

### RGB 相机
- 海康威视 Hikvision MV-CA050-20GC  
- 多线程缓冲池 + HDF5 直接写入  
- 持续 **60FPS 不掉帧**

### DVS 相机
- Prophesee 事件相机  
- 使用 Metavision SDK 录制 EVT3 数据流

### 硬件同步
- Arduino Uno 作为触发器  
- TTL 信号保证两路相机严格同步

---

## 2. 高性能预处理层 (DataProcessor)

### 查表法（Lookup Table Remap）
预先计算坐标映射表，将 **单应性变换 + 裁剪** 融合为一次内存操作，  
单帧处理时间降低 **10 倍**。

### CPU 预索引（Pre-indexing）
对事件数据使用 **O(N) 滑动窗口算法**，预计算每帧 RGB 对应的事件时间窗，  
避免海量事件搜索。

### 并行加速
Remap 与 Voxelization 采用 **OpenMP** 全并行化，充分利用多核心 CPU。

---

## 3. 深度推理层 (Python Inference)

### 胶水层
C++ 使用 `QProcess` 调用 `model/run_inference.py` 并传递路径和配置。

### 数据加载
自定义 `H5ImageDataset`，直接从 C++ 生成的 HDF5 文件零拷贝加载为 PyTorch Tensor。

### 模型
集成 BasicSR 框架，可加载基于事件辅助的去模糊网络（如 EFNet）。

---

## ??? 硬件与环境要求

### 计算平台
- Windows 10/11 x64  
- **CPU：i7 / i9（多核性能关键）**  
- **RAM：32GB+**  
- **GPU：NVIDIA RTX 3060 或更高（≥8GB 显存）**

### 传感器
- Hikvision 工业相机（GigE）  
- Prophesee 事件相机（USB3）  
- Arduino Uno（同步板）

### C++ 依赖
- Visual Studio 2019/2022  
- CMake 3.10+  
- Qt 5.15 (MSVC)  
- OpenCV 4.5+  
- HDF5 1.10+  
- Metavision SDK 4.0+  
- MVS SDK

### Python 依赖
- Python 3.8+  
- PyTorch（CUDA）  
- h5py, opencv-python, pyyaml, tqdm, scipy  
- BasicSR

---

## ?? 安装与构建指南

### 1. 克隆代码
```bash
git clone https://github.com/your-repo/DualCamera-Deblur.git
cd DualCamera-Deblur
