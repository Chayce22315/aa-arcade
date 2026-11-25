# Compiles data files into a C++ translation unit so spindle.exe carries its
# essential resources (Lua scripts, sound effects) and never depends on loose files.

set(SPINDLE_EMBED_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/EmbedScript.cmake)

function(spindle_embed_files)
  cmake_parse_arguments(ARG "" "OUTPUT;BASE_DIR" "FILES" ${ARGN})
  set(deps)
  foreach(f IN LISTS ARG_FILES)
    list(APPEND deps ${ARG_BASE_DIR}/${f})
  endforeach()
  string(REPLACE ";" "|" files_arg "${ARG_FILES}")
  add_custom_command(
    OUTPUT ${ARG_OUTPUT}
    COMMAND ${CMAKE_COMMAND}
      -DOUTPUT=${ARG_OUTPUT}
      -DBASE_DIR=${ARG_BASE_DIR}
      "-DFILES=${files_arg}"
      -P ${SPINDLE_EMBED_SCRIPT}
    DEPENDS ${deps} ${SPINDLE_EMBED_SCRIPT}
    COMMENT "Embedding spindle data files"
    VERBATIM)
endfunction()
