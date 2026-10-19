add_library(opentakengine STATIC
    engine.cpp
    core/geo.cpp
    core/camera.cpp
    raster/tile_loader.cpp
    raster/tile_matrix.cpp
    raster/tile_cache.cpp
    raster/tile_provider.cpp
    raster/mobac.cpp
    elevation/elevation.cpp
    feature/parsers.cpp
    feature/text.cpp
    feature/feature_layer.cpp
    render/globe_renderer.cpp
    render/gfx_impl.cpp)

if(WIN32)
    target_sources(opentakengine PRIVATE net/http_winhttp.cpp)
    target_link_libraries(opentakengine PRIVATE winhttp)
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
        ${earcut_SOURCE_DIR})
target_compile_definitions(opentakengine PUBLIC ${OTE_GFX_BACKEND})
target_link_libraries(opentakengine PRIVATE ote_sqlite pugixml::pugixml ote_miniz)
if(MSVC)
    target_compile_options(opentakengine PRIVATE /W3 /utf-8)
    target_compile_definitions(opentakengine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

ote_add_shaders(opentakengine
    render/shaders/globe.glsl
    render/shaders/surface.glsl
    render/shaders/sky.glsl
    render/shaders/feature.glsl)
ote_embed(opentakengine font_bold ${PROJECT_SOURCE_DIR}/assets/fonts/NotoSans-Bold-subset.ttf)
