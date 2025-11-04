# master_process.py
import os
import argparse
import subprocess
import shutil
import numpy as np
import h5py
import cv2
import scipy.io as sio  # 用于 homography.py
from numpy.lib.recfunctions import structured_to_unstructured # 用于 load_data.py
from PIL import Image # 用于 e2p.py

# (在这里导入您的深度学习库，例如 torch)
# import torch 

# ==================================================================
# 1. 辅助函数 (来自 metavision2h5.py)
# ==================================================================
def convert_raw_to_h5(segment_path, segment_name):
    """
    使用 metavision_file_to_hdf5.exe 将 .raw 转换为 .h5
    """
    print(f"[Step 1/7] Converting {segment_name}.raw to HDF5...")
    
    # 假设 .exe 与此脚本位于同一目录
    base_dir = os.path.dirname(os.path.abspath(__file__))
    exe_path = os.path.join(base_dir, 'metavision_file_to_hdf5.exe')
    
    # 根据 metavision2h5.py 构建路径
    raw_path = os.path.join(segment_path, f"{segment_name}.raw")
    output_path = os.path.join(segment_path, f"{segment_name}_original.h5")
    
    # (注意: 您的 metavision2h5.py 使用了复杂的 relpath。
    #  如果 .exe 和 .py 在同一个根目录，这个简单的路径可能更健壮)
    
    if not os.path.exists(raw_path):
        print(f"  [Error] RAW file not found: {raw_path}")
        return False, None

    try:
        # arguments = ["-i", raw_path, "-o", output_path]
        # output = subprocess.run([exe_path] + arguments, check=True, ...)
        
        # (!!! 在此处填入您的 subprocess.run 逻辑 !!!)
        print(f"  [Success] Converted to {output_path}")
        return True, output_path
    
    except Exception as e:
        print(f"  [Error] Failed to convert RAW to H5: {e}")
        return False, None

# ==================================================================
# 2. 辅助函数 (来自 load_data.py)
# ==================================================================
def convert_h5_to_npy(events_h5_path):
    """
    从 H5 中提取事件和触发器，并保存为 .npy
    """
    print(f"[Step 2/7] Extracting events and triggers from {events_h5_path}...")
    
    output_events_npy = events_h5_path.replace('.h5', '.npy')
    output_trigger_npy = events_h5_path.replace('.h5', '_trigger.npy')
    
    try:
        # with h5py.File(events_h5_path, 'r') as f:
        #     events = f['CD']['events'][:]
        #     events = structured_to_unstructured(events)
        #     events[:, 1] = 719 - events[:, 1] # 翻转 Y 轴
        #     
        #     trigger_data = f['EXT_TRIGGER']['events'][:]
        #     trigger_data = structured_to_unstructured(trigger_data)
        #     
        #     np.save(output_events_npy, events)
        #     np.save(output_trigger_npy, trigger_data)

        # (!!! 在此处填入您的 H5 读取和 np.save 逻辑 !!!)
        
        print(f"  [Success] Saved {output_events_npy}")
        print(f"  [Success] Saved {output_trigger_npy}")
        return True, output_events_npy, output_trigger_npy
        
    except Exception as e:
        print(f"  [Error] Failed to extract from H5: {e}")
        return False, None, None

# ==================================================================
# 3. 辅助函数 (来自 e2p.py)
# ==================================================================
def split_events_to_segments(segment_path, events_npy_path, trigger_npy_path):
    """
    使用 trigger.npy 将 events.npy 拆分为单独的图像和事件片段
    """
    print(f"[Step 3/7] Splitting events into segments...")
    
    try:
        # timestamps = np.load(trigger_npy_path)[:, 1]
        # events = np.load(events_npy_path)
        
        # event_img_dir = os.path.join(segment_path, "event_rgb")
        # event_npy_dir = os.path.join(segment_path, "event_rgb_npy")
        # (清除并重建目录...)

        # (!!! 在此处填入您的 e2p.py 中的 'for i in range(num):' 循环逻辑 !!!)
        # 循环中应包含:
        # 1. 调用 events_to_frame()
        # 2. 保存 image.save(...)
        # 3. 提取 event_slice
        # 4. np.save(output_evt_path, event_slice)

        print(f"  [Success] Event segments created.")
        return True
        
    except Exception as e:
        print(f"  [Error] Failed to split events: {e}")
        return False

# ==================================================================
# 4. 辅助函数 (来自 homography.py)
# ==================================================================
def align_rgb_frames(segment_path, corner_mat_path):
    """
    使用 homography.py 的逻辑对齐 HDF5 中的 RGB 帧
    """
    print(f"[Step 4/7] Aligning RGB frames...")
    
    try:
        # result_dir = os.path.join(segment_path, 'align_viz')
        # image_crop_dir = os.path.join(segment_path, 'rgb_crop')
        # (清除并重建目录...)

        # h5_path = os.path.join(segment_path, 'rgb_data.h5') # 这是 C++ 创建的文件
        # event_rgb_dir = os.path.join(segment_path, "event_rgb") # 这是上一步创建的
        
        # (!!! 在此处填入您的 homography.py 逻辑 !!!)
        # 1. 加载 .mat 角点文件 (dvs, rgb)
        # 2. cv2.findHomography(..., Homo, ...)
        # 3. 'with h5py.File(h5_path, 'r') as f:' 打开 C++ 的 H5 文件
        # 4. 'rgb_frames = f['rgb/frames'][:] (!!! 注意: 您可能还需要读取 /rgb/frame_numbers !!!)
        # 5. 循环 (for i in range(num_frames):)
        # 6. 'imgOut = cv2.warpPerspective(img, Homo, ...)'
        # 7. 'cv2.imwrite(os.path.join(image_crop_dir, ...), imgOut)'
        
        print(f"  [Success] RGB frames aligned and saved to 'rgb_crop'.")
        return True

    except Exception as e:
        print(f"  [Error] Failed to align RGB frames: {e}")
        return False

# ==================================================================
# 5. 辅助函数 (来自 event_crop.py)
# ==================================================================
def crop_event_segments(segment_path):
    """
    空间裁剪 'event_rgb_npy' 文件夹中的 .npy 文件
    """
    print(f"[Step 5/7] Spatially cropping event segments...")

    try:
        # event_dir = os.path.join(segment_path, 'event_rgb_npy')
        # cropped_dir = os.path.join(segment_path, 'event_rgb_npy_cropped')
        # os.makedirs(cropped_dir, exist_ok=True)
        #
        # x_min = 280 #
        
        # (!!! 在此处填入您的 event_crop.py 逻辑 !!!)
        # 1. 'for filename in os.listdir(event_dir):'
        # 2. 'events = np.load(input_path)'
        # 3. 'mask = (x_coords >= x_min) & (x_coords < x_max)'
        # 4. 'cropped_events = events[mask]'
        # 5. 'cropped_events[:, 0] -= x_min'
        # 6. 'np.save(output_path, cropped_events)'
        
        print(f"  [Success] Event segments cropped and saved to 'event_rgb_npy_cropped'.")
        return True

    except Exception as e:
        print(f"  [Error] Failed to crop event segments: {e}")
        return False

# ==================================================================
# 6. 辅助函数 (深度学习去模糊)
# ==================================================================
def run_deblurring(segment_path):
    """
    在对齐的数据上运行您的深度学习去模糊模型
    """
    print(f"[Step 6/7] Loading Deep Learning model...")
    
    # (!!! 在此处填入您的模型加载逻辑 !!!)
    # model = torch.load("path/to/your/model.pth")
    # model.eval()
    
    print(f"[Step 7/7] Running deblurring inference...")
    
    try:
        # 定义输入/输出文件夹
        aligned_rgb_dir = os.path.join(segment_path, 'rgb_crop')
        aligned_event_dir = os.path.join(segment_path, 'event_rgb_npy_cropped')
        deblurred_output_dir = os.path.join(segment_path, 'deblurred')
        os.makedirs(deblurred_output_dir, exist_ok=True)

        rgb_files = sorted([f for f in os.listdir(aligned_rgb_dir) if f.endswith('.png')])
        
        if not rgb_files:
            print("  [Error] No aligned RGB files found in 'rgb_crop'.")
            return False

        # (!!! 在此处填入您的模型推理循环 !!!)
        # for rgb_file in rgb_files:
        #     # 1. 加载对齐的输入
        #     blurry_rgb = cv2.imread(os.path.join(aligned_rgb_dir, rgb_file))
        #     events_npy = np.load(os.path.join(aligned_event_dir, rgb_file.replace('.png', '.npy')))
        #
        #     # 2. (预处理数据: to_tensor, normalize, etc.)
        #
        #     # 3. 运行模型
        #     # with torch.no_grad():
        #     #    deblurred_tensor = model(blurry_tensor, events_tensor)
        #
        #     # 4. (后处理数据: tensor_to_cv2, etc.)
        #     # deblurred_image = ...
        #
        #     # 5. 保存结果
        #     output_path = os.path.join(deblurred_output_dir, rgb_file)
        #     # cv2.imwrite(output_path, deblurred_image)
        #
        #     print(f"  [Processed] {rgb_file}")

        print(f"  [Success] Deblurring complete. Results saved to 'deblurred'.")
        return True

    except Exception as e:
        print(f"  [Error] Failed during deblurring: {e}")
        return False

# ==================================================================
# 主函数
# ==================================================================
def main():
    parser = argparse.ArgumentParser(description="Master processing pipeline for dual camera data.")
    parser.add_argument("--path", required=True, help="Path to the segment folder (e.g., './Test01/segment_1')")
    args = parser.parse_args()
    
    segment_path = os.path.abspath(args.path)
    segment_name = os.path.basename(segment_path)
    
    # (!! 您需要提供角点 .mat 文件的路径 !!)
    corner_mat_path = "G:/0915/dvs_corners.mat" # <-- (硬编码示例)

    print(f"--- STARTING PROCESSING FOR {segment_path} ---")

    # 步骤 1: .raw -> .h5
    ok, h5_path = convert_raw_to_h5(segment_path, segment_name)
    if not ok: return
    
    # 步骤 2: .h5 -> .npy
    ok, npy_path, trigger_path = convert_h5_to_npy(h5_path)
    if not ok: return

    # 步骤 3: 拆分事件
    ok = split_events_to_segments(segment_path, npy_path, trigger_path)
    if not ok: return

    # 步骤 4: 对齐 RGB 帧
    ok = align_rgb_frames(segment_path, corner_mat_path)
    if not ok: return
    
    # 步骤 5: 裁剪事件
    ok = crop_event_segments(segment_path)
    if not ok: return

    # 步骤 6 & 7: 运行深度学习模型
    ok = run_deblurring(segment_path)
    if not ok: return

    print(f"--- PROCESSING FINISHED SUCCESSFULLY ---")
    
    # 关键：通知 C++ GUI
    print("PYTHON_FINISHED", flush=True)

if __name__ == "__main__":
    main()