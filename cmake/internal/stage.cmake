include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)

function(_bundle_collect_entries NAME BASE_DIR FILES DIRECTORIES OUT_REAL OUT_VIRTUAL)
  set(REAL "")
  set(VIRTUAL "")

  foreach(F ${FILES})
    if(IS_ABSOLUTE "${F}")
      set(ABS "${F}")
    else()
      set(ABS "${BASE_DIR}/${F}")
    endif()
    if(NOT EXISTS "${ABS}" OR IS_DIRECTORY "${ABS}")
      _bundle_log(FATAL_ERROR "bundle_add(${NAME}): file not found: ${ABS}")
    endif()
    file(RELATIVE_PATH VPATH "${BASE_DIR}" "${ABS}")
    if(VPATH MATCHES "^\\.\\.")
      _bundle_log(FATAL_ERROR "bundle_add(${NAME}): file '${ABS}' is not under BASE_DIR '${BASE_DIR}' - pass an explicit BASE_DIR that contains it")
    endif()
    list(APPEND REAL "${ABS}")
    list(APPEND VIRTUAL "${VPATH}")
  endforeach()

  foreach(D ${DIRECTORIES})
    if(IS_ABSOLUTE "${D}")
      set(ABS_DIR "${D}")
    else()
      set(ABS_DIR "${BASE_DIR}/${D}")
    endif()
    if(NOT IS_DIRECTORY "${ABS_DIR}")
      _bundle_log(FATAL_ERROR "bundle_add(${NAME}): directory not found: ${ABS_DIR}")
    endif()

    file(GLOB_RECURSE DIR_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE "${ABS_DIR}/*")
    foreach(DF ${DIR_FILES})
      file(RELATIVE_PATH VPATH "${BASE_DIR}" "${DF}")
      list(APPEND REAL "${DF}")
      list(APPEND VIRTUAL "${VPATH}")
    endforeach()
  endforeach()

  list(LENGTH REAL REAL_COUNT)
  if(REAL_COUNT EQUAL 0)
    _bundle_log(FATAL_ERROR "bundle_add(${NAME}): no files to bundle (FILES/DIRECTORIES matched nothing)")
  endif()

  set(${OUT_REAL} "${REAL}" PARENT_SCOPE)
  set(${OUT_VIRTUAL} "${VIRTUAL}" PARENT_SCOPE)
endfunction()

function(_bundle_stage_and_archive NAME OUTPUT_DIR COMPRESSION REAL_FILES VIRTUAL_PATHS OUT_PAK_FILE)
  set(STAGE_DIR "${OUTPUT_DIR}/stage")
  file(REMOVE_RECURSE "${STAGE_DIR}")

  list(LENGTH REAL_FILES COUNT)
  math(EXPR LAST_INDEX "${COUNT} - 1")
  foreach(INDEX RANGE ${LAST_INDEX})
    list(GET REAL_FILES ${INDEX} SRC)
    list(GET VIRTUAL_PATHS ${INDEX} VPATH)
    set(DST "${STAGE_DIR}/${VPATH}")
    get_filename_component(DST_DIR "${DST}" DIRECTORY)
    file(MAKE_DIRECTORY "${DST_DIR}")
    file(COPY "${SRC}" DESTINATION "${DST_DIR}")
  endforeach()

  if(COMPRESSION STREQUAL "STORE")
    set(COMPRESSION_ARGS COMPRESSION None)
  elseif(COMPRESSION STREQUAL "DEFLATE")
    set(COMPRESSION_ARGS "")
  else()
    _bundle_log(FATAL_ERROR "bundle_add(${NAME}): COMPRESSION must be STORE or DEFLATE, got '${COMPRESSION}'")
  endif()

  set(PAK_FILE "${OUTPUT_DIR}/${NAME}.pak")
  file(REMOVE "${PAK_FILE}")
  file(ARCHIVE_CREATE
    OUTPUT "${PAK_FILE}"
    PATHS ${VIRTUAL_PATHS}
    FORMAT zip
    ${COMPRESSION_ARGS}
    WORKING_DIRECTORY "${STAGE_DIR}"
  )

  set(${OUT_PAK_FILE} "${PAK_FILE}" PARENT_SCOPE)
endfunction()
