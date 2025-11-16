# master_process.py
import os
import argparse
import subprocess
import shutil
import numpy as np
import h5py
import cv2
import scipy.io as sio  #
from numpy.lib.recfunctions import structured_to_unstructured #
from PIL import Image #
import yaml # +++ ADDED: 用于 YML 配置

# (在这里导入您的深度学习库，例如 torch)
# (您的 test.py 会处理 torch 的导入)

# ==================================================================
# 1. 辅助函数 (来自 metavision2h5.py)
# ==================================================================
def convert_raw_to_h5(segment_path, segment_name):
    # ... (此函数保持不变) ...
    print(f"[Step 1/7] Converting {segment_name}.raw to HDF5...")
    base_dir = os.path.dirname(os.path.abspath(__file__))
    exe_path = os.path.join(base_dir, 'metavision_file_to_hdf5.exe')
    raw_path = os.path.join(segment_path, f"{segment_name}.raw")
    output_path = os.path.join(segment_path, f"{segment_name}_original.h5")
    
    if not os.path.exists(raw_path):
        print(f"  [Error] RAW file not found: {raw_path}")
        return False, None
    if not os.path.exists(exe_path):
        print(f"  [Error] 转换器未找到: {exe_path}")
        return False, None
    try:
        arguments = ["-i", os.path.abspath(raw_path), "-o", os.path.abspath(output_path)]
        print(f"  [Exec] {' '.join([exe_path] + arguments)}")
        output = subprocess.run([exe_path] + arguments, check=True, capture_output=True, text=True, cwd=base_dir)
        if output.stderr:
             print(f"  [Tool Output] {output.stderr}")
        print(f"  [Success] Converted to {output_path}")
        return True, output_path
    except Exception as e:
        print(f"  [Error] Failed to convert RAW to H5: {e}")
        return False, None

# ==================================================================
# 2. 辅助函数 (来自 load_data.py)
# ==================================================================
def convert_h5_to_npy(events_h5_path):
    # ... (此函数保持不变) ...
    print(f"[Step 2/7] Extracting events and triggers from {events_h5_path}...")
    output_events_npy = events_h5_path.replace('.h5', '.npy')
    output_trigger_npy = events_h5_path.replace('.h5', '_trigger.npy')
    try:
        with h5py.File(events_h5_path, 'r') as f:
            events = f['CD']['events'][:]
            events = structured_to_unstructured(events)
            events[:, 1] = 719 - events[:, 1]
            trigger_data = f['EXT_TRIGGER']['events'][:]
            trigger_data = structured_to_unstructured(trigger_data)
            np.save(output_events_npy, events)
            np.save(output_trigger_npy, trigger_data)
        print(f"  [Success] Saved {output_events_npy}")
        print(f"  [Success] Saved {output_trigger_npy}")
        return True, output_events_npy, output_trigger_npy
    except Exception as e:
        print(f"  [Error] Failed to extract from H5: {e}")
        return False, None, None

# ==================================================================
# 3. 辅助函数 (基于 e2p.py，移除了图像)
# ==================================================================
def split_events_to_segments(segment_path, events_npy_path, trigger_npy_path, c_plus_plus_rgb_frame_count):
    # ... (此函数保持不变) ...
    print(f"[Step 3/7] Splitting events into data segments...")
    try:
        timestamps = np.load(trigger_npy_path)[:, 1]
        events = np.load(events_npy_path)
        event_npy_dir = os.path.join(segment_path, "event_rgb_npy")
        if os.path.exists(event_npy_dir):
            shutil.rmtree(event_npy_dir)
        os.makedirs(event_npy_dir, exist_ok=True)
        
        num = int(min(c_plus_plus_rgb_frame_count, (timestamps.size - 1) / 2))
        timestamps = timestamps[:num * 2]
        print(f"  [Info] Found {len(timestamps)} triggers, matching to {num} RGB frames.")
        
        for i in range(num):
            output_evt_path = os.path.join(event_npy_dir, f'event_{i:06d}.npy')
            start_evt_time = timestamps[i * 2 + 1]
            end_evt_time = timestamps[i * 2 + 3] if (i * 2 + 3) < len(timestamps) else start_evt_time + 6000
            mask = (events[:, 3] >= start_evt_time) & (events[:, 3] < end_evt_time)
            event_slice = events[mask]
            np.save(output_evt_path, event_slice)
            
        print(f"  [Success] {num} event data segments created.")
        return True, num
    except Exception as e:
        print(f"  [Error] Failed to split events: {e}")
        return False, 0

# ==================================================================
# 4. 辅助函数 (基于 homography.py，移除了可视化)
# ==================================================================
def align_rgb_frames(segment_path, num_frames_to_process):
    # ... (此函数保持不变) ...
    print(f"[Step 4/7] Aligning RGB frames (based on homography.py)...")
    try:
        image_crop_dir = os.path.join(segment_path, 'rgb_crop')
        if os.path.exists(image_crop_dir):
            shutil.rmtree(image_crop_dir)
        os.makedirs(image_crop_dir, exist_ok=True)
        
        h5_path = os.path.join(segment_path, 'rgb_data.h5') # C++ 创建的 H5 文件
        
        base_dir = os.path.dirname(os.path.abspath(__file__))
        dvs = sio.loadmat(os.path.join(base_dir, 'dvs_corners.mat'))
        rgb = sio.loadmat(os.path.join(base_dir, 'normal_corners.mat'))
        dvs_points = dvs['dvs_corners'][0]
        rgb_points = rgb['normal_corners'][0]
        point1 = dvs_points[0]
        point2 = np.flip(rgb_points[0], axis=0)
        Homo, error = cv2.findHomography(np.array(point2), np.array(point1), cv2.RANSAC, 1.0, maxIters=10000)
        print(f"  [Info] Homography Matrix calculated.")

        with h5py.File(h5_path, 'r') as f:
            rgb_frames = f['rgb/frames'][:]
            num_frames = min(len(rgb_frames), num_frames_to_process)

        for i in range(num_frames):
            img = rgb_frames[i]
            imgOut = cv2.warpPerspective(img, Homo, (1280, 720))
            imgOut = imgOut[:, 280:]
            save_name = f"{i:06d}.png" 
            cv2.imwrite(os.path.join(image_crop_dir, save_name), imgOut)
            
        print(f"  [Success] {num_frames} RGB frames aligned and saved to 'rgb_crop'.")
        return True
    except Exception as e:
        print(f"  [Error] Failed to align RGB frames: {e}")
        return False

# ==================================================================
# 5. 辅助函数 (<<< MODIFIED: 基于 event_crop.py，保存为 .npz)
# ==================================================================
def crop_event_segments(segment_path):
    """
    空间裁剪 'event_rgb_npy' 文件夹中的 .npy 文件
    并保存为 .npz 以匹配 NpzPngSingleDeblurDataset
    """
    print(f"[Step 5/7] Spatially cropping event segments (based on event_crop.py)...")

    try:
        event_dir = os.path.join(segment_path, 'event_rgb_npy')
        # (!!! 关键: YML 需要 .npz，我们输出到新文件夹)
        cropped_dir = os.path.join(segment_path, 'event_rgb_npz_cropped') 
        
        if not os.path.isdir(event_dir):
            print(f"  [Error] Input folder 'event_rgb_npy' not found.")
            return False
            
        if os.path.exists(cropped_dir):
            shutil.rmtree(cropped_dir)
        os.makedirs(cropped_dir, exist_ok=True)
        
        x_min = 280 #
        x_max = 1280 #
        
        for filename in os.listdir(event_dir):
            if not filename.endswith('.npy'):
                continue

            input_path = os.path.join(event_dir, filename)
            # (!!! 关键: 将 .npy 扩展名改为 .npz)
            output_path = os.path.join(cropped_dir, filename.replace('.npy', '.npz'))

            events = np.load(input_path)
            if events.ndim != 2 or events.shape[1] != 4:
                continue

            x_coords = events[:, 0]
            mask = (x_coords >= x_min) & (x_coords < x_max)
            cropped_events = events[mask]
            
            cropped_events[:, 0] -= x_min #
            
            # (!!! 关键: 保存为 .npz 格式，并命名 'events' 键)
            # (NpzPngSingleDeblurDataset 可能会查找名为 'events' 的键)
            np.savez_compressed(output_path, events=cropped_events)
        
        print(f"  [Success] Event segments cropped and saved to 'event_rgb_npz_cropped'.")
        return True

    except Exception as e:
        print(f"  [Error] Failed to crop event segments: {e}")
        return False

def run_deblurring(segment_path):

    print(f"[Step 6/7] Preparing model configuration...")

    try:
        import yaml
    except ImportError:
        print("  [Error] 'pyyaml' is required to run the model. Please install it:")
        print("  pip install pyyaml")
        return False

    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    # 假设 real.yml 和 test.py 与 master_process.py 在同一目录
    base_yml_path = os.path.join(base_dir, 'real.yml')
    test_script_path = os.path.join(base_dir, 'test.py')
    
    temp_yml_path = os.path.join(segment_path, 'temp_run_config.yml')

    if not os.path.exists(base_yml_path):
        print(f"  [Error] Base config not found: {base_yml_path}")
        return False
    if not os.path.exists(test_script_path):
        print(f"  [Error] Test script not found: {test_script_path}")
        return False

    try:
        with open(base_yml_path, 'r') as f:
            config = yaml.safe_load(f)

        # 2. (!!! 关键: 覆盖路径 !!!)
        # 覆盖输入数据路径
        config['datasets']['test']['dataroot'] = segment_path
        # (NpzPngSingleDeblurDataset 现在会查找:
        #  segment_path/rgb_crop/*.png
        #  segment_path/event_rgb_npz_cropped/*.npz)
        
        # 覆盖输出根路径
        config['path']['root'] = segment_path
        # (make_exp_dirs 现在会在 segment_path/results/demo_car_test/... 中创建日志)
        
        # (!!! 关键: 将我们的 'deblurred' 文件夹设置为保存路径 !!!)
        deblurred_output_dir = os.path.join(segment_path, 'deblurred')
        os.makedirs(deblurred_output_dir, exist_ok=True)
        # (我们必须告诉 test.py 保存到哪里)
        # (basicsr 模型 会保存到 opt['path']['visualization'] / opt['name'])
        config['path']['visualization'] = deblurred_output_dir
        config['name'] = 'final_output' # (这将保存到 deblurred/final_output/)
        config['val']['save_img'] = True


        # 3. 写入临时 YML
        with open(temp_yml_path, 'w') as f:
            yaml.dump(config, f, sort_keys=False)

        print(f"  [Info] Temporary config saved to {temp_yml_path}")

    except Exception as e:
        print(f"  [Error] Failed to create temporary YML: {e}")
        return False

    print(f"[Step 7/7] Running deblurring inference (launching test.py)...")
    
    try:
        # 4. (!!! 关键: 使用 subprocess 调用 test.py !!!)
        # (我们使用 'python'，假设 C++ GUI 启动时已激活 Conda 环境)
        arguments = ["python", test_script_path, "-opt", temp_yml_path]
        
        # (我们不需要 cwd=base_dir，因为所有路径都是绝对的或在 YML 中)
        # (使用 shell=True 或直接调用 python.exe 可能更健壮)
        print(f"  [Exec] {' '.join(arguments)}")
        
        # 实时打印 stdout/stderr
        process = subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8')
        
        for line in process.stdout:
            print(f"  [test.py] {line.strip()}")
            
        process.wait() # 等待进程结束
        
        if process.returncode != 0:
            print(f"  [Error] test.py finished with non-zero exit code: {process.returncode}")
            return False

        print(f"  [Success] Deblurring complete. Results saved to {deblurred_output_dir}/final_output/")
        return True

    except Exception as e:
        print(f"  [Error] Failed during deblurring subprocess: {e}")
        return False

# ==================================================================
# 8. 辅助函数 (获取 C++ HDF5 的帧数)
# ==================================================================
def get_rgb_frame_count(h5_path):
    # ... (此函数保持不变) ...
    print(f"[Step 0/7] Reading frame count from {h5_path}...")
    try:
        with h5py.File(h5_path, 'r') as f:
            if 'rgb/frames' not in f:
                print(f"  [Error] 'rgb/frames' not found in H5 file.")
                return 0
            frame_count = len(f['rgb/frames'])
            print(f"  [Info] Found {frame_count} frames in C++ HDF5 file.")
            return frame_count
    except Exception as e:
        print(f"  [Error] Failed to read H5 file {h5_path}: {e}")
        return 0
        
# ==================================================================
# 主函数
# ==================================================================
def main():
    parser = argparse.ArgumentParser(description="Master processing pipeline for dual camera data.")
    parser.add_argument("--path", required=True, help="Path to the segment folder (e.g., './Test01/segment_1')")
    args = parser.parse_args()
    
    segment_path = os.path.abspath(args.path)
    segment_name = os.path.basename(segment_path)
    
    # 假设角点文件与此脚本放在同一目录
    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    # C++ 创建的 RGB HDF5 文件
    rgb_h5_path = os.path.join(segment_path, 'rgb_data.h5')

    print(f"--- STARTING PROCESSING FOR {segment_path} ---")

    # 步骤 0: 检查输入文件
    if not os.path.exists(rgb_h5_path):
        print(f"  [Error] C++ HDF5 file not found: {rgb_h5_path}")
        return
        
    # 步骤 0.5: 获取 C++ HDF5 的帧数
    num_frames = get_rgb_frame_count(rgb_h5_path)
    if num_frames == 0: return

    # 步骤 1: .raw -> .h5
    ok, h5_path = convert_raw_to_h5(segment_path, segment_name)
    if not ok: return
    
    # 步骤 2: .h5 -> .npy
    ok, npy_path, trigger_path = convert_h5_to_npy(h5_path)
    if not ok: return

    # 步骤 3: 拆分事件
    ok, num_segments = split_events_to_segments(segment_path, npy_path, trigger_path, num_frames)
    if not ok: return

    # 步骤 4: 对齐 RGB 帧
    ok = align_rgb_frames(segment_path, num_segments)
    if not ok: return
    
    # 步骤 5: 裁剪事件
    ok = crop_event_segments(segment_path)
    if not ok: return

    # 步骤 6 & 7: 运行深度学习模型
    # ok = run_deblurring(segment_path)
    # if not ok: return

    print(f"--- PROCESSING FINISHED SUCCESSFULLY ---")
    
    # 关键：通知 C++ GUI
    print("PYTHON_FINISHED", flush=True)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"PYTHON_ERROR: {e}", flush=True)
        # 确保 C++ 知道出错了