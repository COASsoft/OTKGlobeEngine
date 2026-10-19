cmake_minimum_required(VERSION 3.24)
project(OpenTAKEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OTE_BUILD_VIEWER "Build the demo viewer" ON)
option(OTE_WITH_GDAL "Build the optional GDAL raster, elevation and vector module" OFF)

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
FetchContent_Declare(sqlite
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip)
FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.16)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    SOURCE_SUBDIR none)
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
    SOURCE_SUBDIR none)
FetchContent_Declare(earcut
    URL https://raw.githubusercontent.com/mapbox/earcut.hpp/v3.2.4/include/mapbox/earcut.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    SOURCE_SUBDIR none)
FetchContent_Declare(cgltf
    URL https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/cgltf.h
    DOWNLOAD_NO_EXTRACT TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(sokol stb sqlite json miniz earcut cgltf)
add_library(ote_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(ote_miniz PUBLIC ${miniz_SOURCE_DIR})
set(PUGIXML_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pugixml)

add_library(ote_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(ote_sqlite PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(ote_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(ote_sqlite PUBLIC Threads::Threads)
endif()

include(cmake/shaders.cmake)
include(cmake/embed.cmake)

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
