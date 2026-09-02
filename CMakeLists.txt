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

# ESP-IDF 5.5.4 的 20260121 工具链会先写入绝对 Picolibc specs 路径，
# newlib 组件又追加同一份 specs 的短路径，GCC 会因重复加载而中止。
# 仅在绝对路径已经存在时移除重复短路径，其他工具链保持官方默认行为。
file(STRINGS "${IDF_TOOLCHAIN_BUILD_DIR}/cflags" PROJECT_PICOLIBC_SPEC_FLAGS
     REGEX "build/specs/picolibc\\.specs")
if(CONFIG_LIBC_PICOLIBC AND PROJECT_PICOLIBC_SPEC_FLAGS)
    idf_toolchain_remove_flags(COMPILE_OPTIONS "^-specs=picolibc\\.specs$")
endif()
