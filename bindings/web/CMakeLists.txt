file(STRINGS ${PROJECT_SOURCE_DIR}/include/otk/otk.h _otk_decls REGEX "^OTK_API .*[(]")
set(_otk_exports "_otk_web_create" "_malloc" "_free")
foreach(_decl IN LISTS _otk_decls)
    if(_decl MATCHES "[ *](otk_[a-z0-9_]+)[(]")
        list(APPEND _otk_exports "_${CMAKE_MATCH_1}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _otk_exports)
string(REPLACE ";" "," _otk_exports "${_otk_exports}")

add_executable(otkglobeengine_web src/otk_web.cpp)
target_link_libraries(otkglobeengine_web PRIVATE otkglobeengine)
set_target_properties(otkglobeengine_web PROPERTIES OUTPUT_NAME otkglobeengine SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/web)
target_link_options(otkglobeengine_web PRIVATE
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createOTKModule
    -sENVIRONMENT=web,worker
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=256MB
    -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=2MB
    -sPTHREAD_POOL_SIZE=24
    -sPTHREAD_POOL_SIZE_STRICT=0
    -sWASMFS=1
    -sFORCE_FILESYSTEM=1
    -sFETCH=1
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=2
    "-sEXPORTED_FUNCTIONS=${_otk_exports}"
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,FS,HEAPU8,HEAP32,HEAPF64,getValue,setValue"
    $<$<CONFIG:Release>:-O3>)
