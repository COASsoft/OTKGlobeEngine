cmake_minimum_required(VERSION 3.16)
project(otk_package_consumer C)

find_package(OTKGlobeEngine 0.1 REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE OTKGlobeEngine::otkglobeengine_c)
if(UNIX)
    target_link_libraries(consumer PRIVATE m)
endif()
