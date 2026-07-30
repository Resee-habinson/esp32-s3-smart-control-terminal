# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Keep the upstream XiaoZhi warning policy for ESP-IDF 6.
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Include only main and its declared dependencies.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.4.0")
project(ESP32-S3-AI)