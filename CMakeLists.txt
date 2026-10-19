cmake_minimum_required(VERSION 3.24)
project(OpenTAKEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OTE_BUILD_VIEWER "Build the demo viewer" ON)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(sokol
    GIT_REPOSITORY https://github.com/floooh/sokol.git
    GIT_TAG efeb77603e24daea7d1dd6445b9358bb5d82533b)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(sokol stb)

include(cmake/shaders.cmake)

if(WIN32)
    set(OTE_GFX_BACKEND SOKOL_D3D11)
    set(OTE_SHDC_LANG hlsl5)
elseif(APPLE)
    set(OTE_GFX_BACKEND SOKOL_METAL)
    if(IOS)
        set(OTE_SHDC_LANG metal_ios)
    else()
        set(OTE_SHDC_LANG metal_macos)
    endif()
elseif(ANDROID OR EMSCRIPTEN)
    set(OTE_GFX_BACKEND SOKOL_GLES3)
    set(OTE_SHDC_LANG glsl300es)
else()
    set(OTE_GFX_BACKEND SOKOL_GLCORE)
    set(OTE_SHDC_LANG glsl410)
endif()

add_subdirectory(src)

if(OTE_BUILD_VIEWER)
    add_subdirectory(apps/viewer)
endif()
