add_library(opentakengine STATIC
    engine.cpp
    input/input_controller.cpp
    net/http_faults.cpp
    net/reachability.cpp
    formats/formats.cpp
    formats/cot.cpp
    formats/text_formats.cpp
    formats/exporters.cpp
    formats/package.cpp
    capi.cpp
    core/geo.cpp
    core/camera.cpp
    core/mgrs.cpp
    core/navigation.cpp
    core/wmm.cpp
    core/geoid.cpp
    raster/tile_loader.cpp
    raster/tile_matrix.cpp
    raster/tile_cache.cpp
    raster/tile_package.cpp
    raster/tile_provider.cpp
    raster/mobac.cpp
    raster/vector_tile.cpp
    elevation/elevation.cpp
    elevation/quantized_mesh.cpp
    feature/parsers.cpp
    feature/text.cpp
    feature/feature_layer.cpp
    feature/grid.cpp
    feature/milsym.cpp
    model/model_layer.cpp
    model/gltf_codecs.cpp
    model/tile_content.cpp
    model/pointcloud.cpp
    tiles3d/tileset.cpp
    render/globe_renderer.cpp
    render/gfx_impl.cpp)

if(WIN32)
    target_sources(opentakengine PRIVATE net/http_winhttp.cpp)
    target_link_libraries(opentakengine PRIVATE winhttp)
    if(OTE_GFX_BACKEND STREQUAL "SOKOL_GLCORE")
        target_link_libraries(opentakengine PUBLIC opengl32)
    endif()
elseif(APPLE)
    target_sources(opentakengine PRIVATE net/http_apple.mm)
    set_source_files_properties(render/gfx_impl.cpp PROPERTIES LANGUAGE OBJCXX)
    target_compile_options(opentakengine PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(opentakengine PUBLIC "-framework Foundation" "-framework Metal" "-framework QuartzCore")
elseif(ANDROID)
    target_sources(opentakengine PRIVATE net/http_android.cpp)
    target_link_libraries(opentakengine PUBLIC GLESv3 EGL log android)
else()
    find_package(CURL REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    target_sources(opentakengine PRIVATE net/http_curl.cpp)
    target_link_libraries(opentakengine PRIVATE CURL::libcurl PUBLIC OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
endif()

target_include_directories(opentakengine
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${sokol_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${stb_SOURCE_DIR}
        ${json_SOURCE_DIR}
        ${earcut_SOURCE_DIR}
        ${cgltf_SOURCE_DIR})
target_compile_definitions(opentakengine PUBLIC ${OTE_GFX_BACKEND})
target_link_libraries(opentakengine PRIVATE ote_sqlite pugixml::pugixml ote_miniz)
foreach(_codec DRACO MESHOPT KTX2)
    if(OTE_WITH_${_codec})
        target_compile_definitions(opentakengine PRIVATE OTE_WITH_${_codec})
    endif()
endforeach()
if(OTE_WITH_DRACO)
    target_link_libraries(opentakengine PRIVATE ote_draco)
endif()
if(OTE_WITH_MESHOPT)
    target_link_libraries(opentakengine PRIVATE ote_meshopt)
endif()
if(OTE_WITH_KTX2)
    target_link_libraries(opentakengine PRIVATE ote_basisu)
endif()
if(OTE_WITH_GDAL)
    find_package(GDAL CONFIG)
    if(NOT GDAL_FOUND)
        find_package(GDAL REQUIRED)
    endif()
    target_sources(opentakengine PRIVATE gdal/gdal_sources.cpp)
    target_link_libraries(opentakengine PRIVATE GDAL::GDAL)
    target_compile_definitions(opentakengine PUBLIC OTE_WITH_GDAL)
else()
    target_sources(opentakengine PRIVATE gdal/gdal_stub.cpp)
endif()
if(MSVC)
    target_compile_options(opentakengine PRIVATE /W3 /utf-8)
    target_compile_definitions(opentakengine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

ote_add_shaders(opentakengine
    render/shaders/globe.glsl
    render/shaders/surface.glsl
    render/shaders/sky.glsl
    render/shaders/feature.glsl
    render/shaders/mip.glsl
    render/shaders/model.glsl
    render/shaders/vector.glsl
    render/shaders/points.glsl)
ote_embed(opentakengine font_bold ${PROJECT_SOURCE_DIR}/assets/fonts/NotoSans-Bold-subset.ttf)
ote_embed(opentakengine geoid_egm96 ${PROJECT_SOURCE_DIR}/assets/geoid/egm96-1deg.bin)
if(OTE_EMBED_BACKDROP)
    ote_embed(opentakengine world_backdrop ${PROJECT_SOURCE_DIR}/assets/world/blue-marble-2048.jpg)
    target_compile_definitions(opentakengine PRIVATE OTE_EMBED_BACKDROP)
endif()

add_library(opentakengine_c SHARED capi_shared.cpp)
target_link_libraries(opentakengine_c PRIVATE opentakengine)
target_compile_definitions(opentakengine_c PRIVATE OTE_SHARED OTE_BUILDING)
if(MSVC)
    target_link_options(opentakengine_c PRIVATE /WHOLEARCHIVE:opentakengine)
elseif(APPLE)
    target_link_options(opentakengine_c PRIVATE -Wl,-force_load,$<TARGET_FILE:opentakengine>)
else()
    target_link_options(opentakengine_c PRIVATE -Wl,--whole-archive $<TARGET_FILE:opentakengine> -Wl,--no-whole-archive)
endif()
set_source_files_properties(capi.cpp PROPERTIES COMPILE_DEFINITIONS "OTE_SHARED;OTE_BUILDING")
if(WIN32 AND OTE_BUILD_VIEWER)
    add_custom_command(TARGET opentakengine_c POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:opentakengine_c> ${CMAKE_BINARY_DIR})
endif()

if(ANDROID)
    add_library(opentakengine_jni SHARED ${PROJECT_SOURCE_DIR}/bindings/android/jni/ote_jni.cpp)
    target_link_libraries(opentakengine_jni PRIVATE opentakengine)
endif()
