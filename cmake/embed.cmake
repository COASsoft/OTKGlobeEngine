function(otk_embed target symbol file)
    get_filename_component(abs "${file}" ABSOLUTE)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/embed/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -DIN=${abs} -DOUT=${out} -DSYMBOL=${symbol} -P "${PROJECT_SOURCE_DIR}/cmake/embed_run.cmake"
        DEPENDS "${abs}" "${PROJECT_SOURCE_DIR}/cmake/embed_run.cmake"
        COMMENT "embed ${symbol}"
        VERBATIM)
    target_sources(${target} PRIVATE "${out}")
endfunction()
