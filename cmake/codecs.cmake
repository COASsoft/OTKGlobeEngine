option(OTE_WITH_DRACO "Decode Draco-compressed glTF meshes and 3D Tiles point clouds" ON)
option(OTE_WITH_MESHOPT "Decode meshoptimizer-compressed glTF buffers" ON)
option(OTE_WITH_KTX2 "Decode KTX2 / Basis Universal textures" ON)

if(OTE_WITH_MESHOPT)
    FetchContent_Declare(meshoptimizer
        URL https://github.com/zeux/meshoptimizer/archive/refs/tags/v1.3.tar.gz
        URL_HASH SHA256=ca4f47d451201593904b9e3067fa54b65db94bc34ab5173864950862b748a145
        SOURCE_SUBDIR none)
    FetchContent_MakeAvailable(meshoptimizer)
    add_library(ote_meshopt STATIC
        ${meshoptimizer_SOURCE_DIR}/src/vertexcodec.cpp
        ${meshoptimizer_SOURCE_DIR}/src/indexcodec.cpp
        ${meshoptimizer_SOURCE_DIR}/src/vertexfilter.cpp)
    target_include_directories(ote_meshopt PUBLIC ${meshoptimizer_SOURCE_DIR}/src)
endif()

if(OTE_WITH_DRACO)
    FetchContent_Declare(draco
        URL https://github.com/google/draco/archive/refs/tags/1.5.7.tar.gz
        URL_HASH SHA256=bf6b105b79223eab2b86795363dfe5e5356050006a96521477973aba8f036fe1
        SOURCE_SUBDIR none)
    FetchContent_MakeAvailable(draco)
    set(_draco ${draco_SOURCE_DIR}/src/draco)
    file(GLOB_RECURSE _draco_sources
        ${_draco}/attributes/*.cc
        ${_draco}/compression/*.cc
        ${_draco}/core/*.cc
        ${_draco}/mesh/*.cc
        ${_draco}/metadata/*.cc
        ${_draco}/point_cloud/*.cc)
    list(FILTER _draco_sources EXCLUDE REGEX "(_test|test_utils|encode|encoder|encoders_controller|draco_compression_options)\\.cc$")
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/draco_generated/draco/draco_features.h CONTENT
"#ifndef DRACO_FEATURES_H_
#define DRACO_FEATURES_H_
#define DRACO_MESH_COMPRESSION_SUPPORTED
#define DRACO_NORMAL_ENCODING_SUPPORTED
#define DRACO_STANDARD_EDGEBREAKER_SUPPORTED
#define DRACO_PREDICTIVE_EDGEBREAKER_SUPPORTED
#define DRACO_POINT_CLOUD_COMPRESSION_SUPPORTED
#define DRACO_BACKWARDS_COMPATIBILITY_SUPPORTED
#endif
")
    add_library(ote_draco STATIC ${_draco_sources})
    target_include_directories(ote_draco PUBLIC ${draco_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/draco_generated)
    if(MSVC)
        target_compile_options(ote_draco PRIVATE /w /bigobj)
        target_compile_definitions(ote_draco PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(ote_draco PRIVATE -w)
    endif()
endif()

if(OTE_WITH_KTX2)
    set(_basisu_tag v2_50)
    set(_basisu_dir ${CMAKE_BINARY_DIR}/_deps/basisu-${_basisu_tag})
    set(_basisu_files
        transcoder/basisu.h transcoder/basisu_astc_cfgs.inl transcoder/basisu_astc_hdr_core.h transcoder/basisu_astc_helpers.h
        transcoder/basisu_containers.h transcoder/basisu_containers_impl.h transcoder/basisu_dds_transcoder.inl
        transcoder/basisu_etc1_mods.inl transcoder/basisu_file_headers.h transcoder/basisu_idct.h transcoder/basisu_transcoder.cpp
        transcoder/basisu_transcoder.h transcoder/basisu_transcoder_internal.h transcoder/basisu_transcoder_tables_astc.inc
        transcoder/basisu_transcoder_tables_astc_0_255.inc transcoder/basisu_transcoder_tables_atc_55.inc
        transcoder/basisu_transcoder_tables_atc_56.inc transcoder/basisu_transcoder_tables_bc7_m5_alpha.inc
        transcoder/basisu_transcoder_tables_bc7_m5_color.inc transcoder/basisu_transcoder_tables_dxt1_5.inc
        transcoder/basisu_transcoder_tables_dxt1_6.inc transcoder/basisu_transcoder_tables_pvrtc2_45.inc
        transcoder/basisu_transcoder_tables_pvrtc2_alpha_33.inc transcoder/basisu_transcoder_uastc.h
        transcoder/basisu_xbc7_decoder.h transcoder/basisu_xbc7_decoder.inl
        zstd/zstd.h zstd/zstd_errors.h zstd/zstddeclib.c zstd/LICENSE LICENSE)
    foreach(_f IN LISTS _basisu_files)
        if(NOT EXISTS ${_basisu_dir}/${_f})
            file(DOWNLOAD https://raw.githubusercontent.com/BinomialLLC/basis_universal/${_basisu_tag}/${_f} ${_basisu_dir}/${_f}.part
                STATUS _basisu_status TLS_VERIFY ON)
            list(GET _basisu_status 0 _basisu_code)
            if(NOT _basisu_code EQUAL 0)
                message(FATAL_ERROR "Downloading basis_universal ${_f} failed: ${_basisu_status}")
            endif()
            file(RENAME ${_basisu_dir}/${_f}.part ${_basisu_dir}/${_f})
        endif()
    endforeach()
    add_library(ote_basisu STATIC ${_basisu_dir}/transcoder/basisu_transcoder.cpp ${_basisu_dir}/zstd/zstddeclib.c)
    target_include_directories(ote_basisu PUBLIC ${_basisu_dir}/transcoder)
    target_compile_definitions(ote_basisu PUBLIC BASISD_SUPPORT_KTX2=1 BASISD_SUPPORT_KTX2_ZSTD=1
        BASISD_SUPPORT_PVRTC1=0 BASISD_SUPPORT_PVRTC2=0 BASISD_SUPPORT_ATC=0 BASISD_SUPPORT_FXT1=0)
    if(MSVC)
        target_compile_options(ote_basisu PRIVATE /w /bigobj)
    else()
        target_compile_options(ote_basisu PRIVATE -w)
    endif()
endif()

option(OTE_WITH_WEBP "Decode WebP imagery, elevation and textures" ON)

if(OTE_WITH_WEBP)
    FetchContent_Declare(libwebp
        URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.5.0.tar.gz
        URL_HASH SHA256=668c9aba45565e24c27e17f7aaf7060a399f7f31dba6c97a044e1feacb930f37
        SOURCE_SUBDIR none)
    FetchContent_MakeAvailable(libwebp)
    set(_webp ${libwebp_SOURCE_DIR}/src)
    file(GLOB _webp_sources ${_webp}/dec/*.c ${_webp}/dsp/*.c ${_webp}/utils/*.c)
    list(FILTER _webp_sources EXCLUDE REGEX "/(enc|cost|ssim|lossless_enc)[^/]*\.c$")
    list(FILTER _webp_sources EXCLUDE REGEX "/(bit_writer_utils|huffman_encode_utils|quant_levels_utils)\.c$")
    add_library(ote_webp STATIC ${_webp_sources})
    target_include_directories(ote_webp PUBLIC ${libwebp_SOURCE_DIR} ${libwebp_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(ote_webp PRIVATE /w)
        target_compile_definitions(ote_webp PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(ote_webp PRIVATE -w)
        find_package(Threads REQUIRED)
        target_link_libraries(ote_webp PRIVATE Threads::Threads)
    endif()
endif()
