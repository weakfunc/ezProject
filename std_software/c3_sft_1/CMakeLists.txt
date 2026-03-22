# ESP-IDF 项目 CMakeLists.txt（根目录）
# 以下三行顺序固定，不可更改
cmake_minimum_required(VERSION 3.16)

# 包含 ESP-IDF 的 CMake 工具链入口
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 最小化构建：只编译 main 组件及其依赖项，减小固件体积
idf_build_set_property(MINIMAL_BUILD ON)

# 项目名称（生成的固件文件将以此命名）
project(c3_sft_1)
