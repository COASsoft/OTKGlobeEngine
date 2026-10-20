cmake_minimum_required(VERSION 3.16)
project(ote_package_consumer C)

find_package(OpenTAKEngine 0.1 REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE OpenTAKEngine::opentakengine_c)
