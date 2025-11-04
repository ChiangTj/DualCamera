cmake_minimum_required(VERSION 3.10)

project(DualCamera LANGUAGES CXX) 

# 【optional】设置工程包含当前目录，非必须
SET(CMAKE_INCLUDE_CURRENT_DIR ON) 

# 设置自动生成moc文件,AUTOMOC打开可以省去QT5_WRAP_CPP命令
SET(CMAKE_AUTOMOC ON)
# 设置自动生成ui.h文件,AUTOUIC打开可以省去QT5_WRAP_UI命令
SET(CMAKE_AUTOUIC ON)
# 【optional】设置自动处理.qrc文件
SET(CMAKE_AUTORCC ON)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add MVCamera paths
set(MV_LIB_DIR "C:/Program Files (x86)/MVS/Development/Libraries/win64")
set(MV_INCLUDE_DIR "C:/Program Files (x86)/MVS/Development/Includes")


list(APPEND CMAKE_PREFIX_PATH "D:/Prophesee/share/cmake/hdf5_ecf"
"D:/Prophesee/share/cmake/MetavisionHAL"
"D:/Prophesee/share/cmake/MetavisionSDK")

find_package(MetavisionSDK COMPONENTS core driver ui REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Qt5 COMPONENTS Widgets Core Gui REQUIRED)
message(STATUS "Qt5 DIR: ${Qt5_DIR}")
find_package(OpenMP REQUIRED)

aux_source_directory(./src BASE_SOURCE)

add_executable(dualcamera ${BASE_SOURCE} main.cpp)

target_include_directories(dualcamera PUBLIC 
    ${OpenCV_INCLUDE_DIRS}
    ${MV_INCLUDE_DIR}
)

target_link_libraries(dualcamera 
    ${OpenCV_LIBRARIES} 
    Qt5::Widgets 
    Qt5::Core 
    Qt5::Gui 
    MetavisionSDK::core 
    MetavisionSDK::driver 
    MetavisionSDK::ui
    ${MV_LIB_DIR}/MvCameraControl.lib
)
