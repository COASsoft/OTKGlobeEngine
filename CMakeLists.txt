cmake_minimum_required(VERSION 3.24)
project(OpenTAKEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(CMAKE_SYSTEM_NAME MATCHES "BSD|DragonFly")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OTE_BUILD_VIEWER "Build the demo viewer" ${PROJECT_IS_TOP_LEVEL})
option(OTE_WITH_GDAL "Build the optional GDAL raster, elevation and vector module" OFF)
option(OTE_EMBED_BACKDROP "Embed a low-resolution world image shown wherever no imagery is available offline" ON)
option(OTE_PACKAGING "Install rules and CPack packages" ${PROJECT_IS_TOP_LEVEL})
set(OTE_SQLITE_TARGET "" CACHE STRING "An existing SQLite target to link instead of building the bundled amalgamation")

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
FetchContent_MakeAvailable(sokol stb json miniz earcut cgltf)
add_library(ote_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(ote_miniz PUBLIC ${miniz_SOURCE_DIR})
set(PUGIXML_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(PUGIXML_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pugixml)

if(OTE_SQLITE_TARGET)
    add_library(ote_sqlite INTERFACE)
    target_link_libraries(ote_sqlite INTERFACE ${OTE_SQLITE_TARGET})
else()
    FetchContent_MakeAvailable(sqlite)
    add_library(ote_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
    target_include_directories(ote_sqlite PUBLIC ${sqlite_SOURCE_DIR})
    target_compile_definitions(ote_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
    if(UNIX AND NOT APPLE AND NOT ANDROID)
        find_package(Threads REQUIRED)
        target_link_libraries(ote_sqlite PUBLIC Threads::Threads)
    endif()
endif()

include(cmake/shaders.cmake)
include(cmake/embed.cmake)
include(cmake/codecs.cmake)

if(OTE_GFX_BACKEND AND OTE_SHDC_LANG)
elseif(WIN32)
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

if(OTE_BUILD_VIEWER AND UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(X11)
    if(NOT X11_FOUND OR NOT X11_Xi_FOUND OR NOT X11_Xcursor_FOUND)
        message(WARNING "X11 with Xi and Xcursor not found; building without the viewer")
        set(OTE_BUILD_VIEWER OFF)
    endif()
endif()

if(OTE_BUILD_VIEWER)
    add_subdirectory(apps/viewer)
endif()

if(WIN32 AND OTE_BUILD_VIEWER)
    add_executable(ote_capi_d3d11 tests/capi_d3d11.cpp)
    target_link_libraries(ote_capi_d3d11 PRIVATE opentakengine_c d3d11)
    add_custom_command(TARGET ote_capi_d3d11 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:opentakengine_c> $<TARGET_FILE_DIR:ote_capi_d3d11>)
    add_test(NAME capi_input COMMAND ote_capi_d3d11 ${CMAKE_BINARY_DIR}/capi_test.png)
    add_test(NAME capi_pool_stress COMMAND ote_capi_d3d11 --pool-stress ${CMAKE_BINARY_DIR}/capi_pool.png)
    target_include_directories(ote_capi_d3d11 PRIVATE ${PROJECT_SOURCE_DIR}/include ${stb_SOURCE_DIR})
    target_compile_definitions(ote_capi_d3d11 PRIVATE OTE_SHARED)
endif()

option(OTE_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
if(OTE_BUILD_TESTS AND NOT ANDROID AND NOT IOS)
    enable_testing()
    add_executable(ote_unit tests/unit.cpp)
    target_link_libraries(ote_unit PRIVATE opentakengine ote_sqlite)
    target_include_directories(ote_unit PRIVATE ${PROJECT_SOURCE_DIR}/src ${stb_SOURCE_DIR} ${json_SOURCE_DIR})
    target_compile_definitions(ote_unit PRIVATE OTE_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data")
    target_include_directories(ote_unit PRIVATE ${cgltf_SOURCE_DIR})
    foreach(_codec DRACO MESHOPT KTX2)
        if(OTE_WITH_${_codec})
            target_compile_definitions(ote_unit PRIVATE OTE_WITH_${_codec})
        endif()
    endforeach()
    if(OTE_WITH_MESHOPT)
        target_link_libraries(ote_unit PRIVATE ote_meshopt)
    endif()
    if(MSVC)
        target_compile_options(ote_unit PRIVATE /utf-8)
        target_compile_definitions(ote_unit PRIVATE NOMINMAX)
    endif()
    add_test(NAME unit COMMAND ote_unit)
    add_library(ote_header_macros OBJECT tests/x11_macros.cpp)
    target_include_directories(ote_header_macros PRIVATE ${PROJECT_SOURCE_DIR}/include)
endif()

if(OTE_PACKAGING AND NOT ANDROID AND NOT IOS)
    include(cmake/packaging.cmake)
endif()
