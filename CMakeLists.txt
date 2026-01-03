cmake_minimum_required(VERSION 3.20)

project(BrailleDisplayCalibrationTool LANGUAGES CXX RC)

add_executable(BrailleDisplayCalibrationTool WIN32
    src/main.cpp
    src/app.rc
)

target_compile_features(BrailleDisplayCalibrationTool PRIVATE cxx_std_17)
target_compile_definitions(BrailleDisplayCalibrationTool PRIVATE UNICODE _UNICODE)
target_include_directories(BrailleDisplayCalibrationTool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
