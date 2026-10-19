set(OTE_SHDC_COMMIT b975d5642093d62e76a1e088bdae945d6d8b8ff2)

if(CMAKE_HOST_WIN32)
    set(_shdc_dir win32)
    set(_shdc_name sokol-shdc.exe)
elseif(CMAKE_HOST_APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_shdc_dir osx_arm64)
    else()
        set(_shdc_dir osx)
    endif()
    set(_shdc_name sokol-shdc)
else()
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_shdc_dir linux_arm64)
    else()
        set(_shdc_dir linux)
    endif()
    set(_shdc_name sokol-shdc)
endif()

set(OTE_SHDC "${CMAKE_BINARY_DIR}/tools/${_shdc_name}")
if(NOT EXISTS "${OTE_SHDC}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/floooh/sokol-tools-bin/${OTE_SHDC_COMMIT}/bin/${_shdc_dir}/${_shdc_name}"
        "${OTE_SHDC}" STATUS _shdc_status TLS_VERIFY ON)
    list(GET _shdc_status 0 _shdc_code)
    if(NOT _shdc_code EQUAL 0)
        message(FATAL_ERROR "Downloading sokol-shdc failed: ${_shdc_status}")
    endif()
    if(NOT CMAKE_HOST_WIN32)
        file(CHMOD "${OTE_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endif()

function(ote_add_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    set(outputs)
    foreach(src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME_WE)
        set(out "${out_dir}/${name}.glsl.h")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND "${OTE_SHDC}" --input "${abs}" --output "${out}" --slang "${OTE_SHDC_LANG}" --format sokol
            DEPENDS "${abs}"
            COMMENT "sokol-shdc ${name}"
            VERBATIM)
        list(APPEND outputs "${out}")
    endforeach()
    target_sources(${target} PRIVATE ${outputs})
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
