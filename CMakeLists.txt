cmake_minimum_required(VERSION 3.10)

project(DualCamera LANGUAGES CXX)

# ==============================================
# 1. 基本设置
# ==============================================

# 包含当前目录（对于 Qt MOC 很有用）
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt 自动化设置
set(CMAKE_AUTOMOC ON) # 自动处理 Q_OBJECT
set(CMAKE_AUTOUIC ON) # 自动处理 .ui
set(CMAKE_AUTORCC ON) # 自动处理 .qrc

# 编译选项
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================
# 2. 第三方库路径配置
# ==============================================

# --- MVS (Hikvision) ---
set(MV_LIB_DIR "C:/Program Files (x86)/MVS/Development/Libraries/win64")
set(MV_INCLUDE_DIR "C:/Program Files (x86)/MVS/Development/Includes")

# --- Prophesee Metavision ---
list(APPEND CMAKE_PREFIX_PATH 
    "D:/Prophesee/share/cmake/hdf5_ecf"
    "D:/Prophesee/share/cmake/MetavisionHAL"
    "D:/Prophesee/share/cmake/MetavisionSDK"
)

# --- HDF5 ---
set(HDF5_DIR "D:/anaconda/Library/cmake") # 注意：通常指向包含 hdf5-config.cmake 的目录

# ==============================================
# 3. 查找依赖包
# ==============================================

# OpenMP
find_package(OpenMP REQUIRED)

# HDF5
find_package(HDF5 COMPONENTS C CXX HL REQUIRED)

# Metavision
find_package(MetavisionSDK COMPONENTS core driver ui REQUIRED)

# OpenCV
find_package(OpenCV REQUIRED)

# Qt5
find_package(Qt5 COMPONENTS Widgets Core Gui REQUIRED)

message(STATUS "Qt5 DIR: ${Qt5_DIR}")
message(STATUS "OpenCV DIR: ${OpenCV_DIR}")
message(STATUS "HDF5 Libraries: ${HDF5_LIBRARIES}")

# ==============================================
# 4. 源文件管理
# ==============================================

# 扫描 src 目录下的所有源文件
aux_source_directory(./src BASE_SOURCE)

# 手动添加头文件（为了让 IDE 显示它们，并让 AUTOMOC 扫描它们）
file(GLOB HEADER_FILES "include/*.h")

# 定义可执行文件
add_executable(dualcamera 
    main.cpp
    ${BASE_SOURCE}
    ${HEADER_FILES}
)

# ==============================================
# 5. 包含目录 (Include Directories)
# ==============================================

target_include_directories(dualcamera PUBLIC
    include
    ${OpenCV_INCLUDE_DIRS}
    ${MV_INCLUDE_DIR}
    ${HDF5_INCLUDE_DIRS}
    ${MetavisionSDK_INCLUDE_DIRS} # 确保包含 Metavision 头文件
)

# ==============================================
# 6. 链接库 (Link Libraries)
# ==============================================

target_link_libraries(dualcamera
    PRIVATE
    ${OpenCV_LIBRARIES}
    Qt5::Widgets
    Qt5::Core
    Qt5::Gui
    MetavisionSDK::core
    MetavisionSDK::driver
    MetavisionSDK::ui
    "${MV_LIB_DIR}/MvCameraControl.lib" # 显式链接静态库/导入库
    ${HDF5_LIBRARIES}     # 通常包含 C 和 CXX
    ${HDF5_HL_LIBRARIES}  # High Level 接口
)

# 链接 OpenMP
if(OpenMP_CXX_FOUND)
    target_link_libraries(dualcamera PRIVATE OpenMP::OpenMP_CXX)
endif()

# ==============================================
# 7. 部署 (可选: 拷贝 DLL 或配置文件)
# ==============================================

if(WIN32)
    add_custom_command(TARGET dualcamera POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/homography.xml"
        "$<TARGET_FILE_DIR:dualcamera>"
        COMMENT "Copying homography.xml to output directory"
    )
endif()