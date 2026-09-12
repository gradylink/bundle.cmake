include(${CMAKE_CURRENT_LIST_DIR}/internal/utils.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/internal/runtime.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/internal/stage.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/internal/codegen.cmake)

set(_BUNDLE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

get_property(_BUNDLE_ENABLED_LANGUAGES GLOBAL PROPERTY ENABLED_LANGUAGES)
if(NOT "C" IN_LIST _BUNDLE_ENABLED_LANGUAGES)
  enable_language(C)
endif()
if(NOT "ASM" IN_LIST _BUNDLE_ENABLED_LANGUAGES)
  enable_language(ASM)
endif()

# bundle_add(<name>
#   [BASE_DIR <dir>]
#   [FILES <file>...]
#   [DIRECTORIES <dir>...]
#   [COMPRESSION STORE|DEFLATE]
#   [OUTPUT_DIR <dir>]
# )
function(bundle_add NAME)
  _bundle_valid_identifier("${NAME}" VALID_NAME)
  if(NOT VALID_NAME)
    _bundle_log(FATAL_ERROR "Invalid bundle name: ${NAME}")
  endif()
  if(TARGET ${NAME})
    _bundle_log(FATAL_ERROR "bundle_add(${NAME}): a target named '${NAME}' already exists")
  endif()

  set(oneValueArgs BASE_DIR COMPRESSION OUTPUT_DIR)
  set(multiValueArgs FILES DIRECTORIES)
  cmake_parse_arguments(BUNDLE "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT BUNDLE_FILES AND NOT BUNDLE_DIRECTORIES)
    _bundle_log(FATAL_ERROR "bundle_add(${NAME}): give at least one of FILES or DIRECTORIES")
  endif()

  if(NOT BUNDLE_BASE_DIR)
    set(BUNDLE_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  get_filename_component(BUNDLE_BASE_DIR "${BUNDLE_BASE_DIR}" ABSOLUTE)

  if(NOT BUNDLE_COMPRESSION)
    set(BUNDLE_COMPRESSION "DEFLATE")
  endif()

  if(NOT BUNDLE_OUTPUT_DIR)
    set(BUNDLE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bundle/${NAME}")
  endif()

  _bundle_collect_entries("${NAME}" "${BUNDLE_BASE_DIR}" "${BUNDLE_FILES}" "${BUNDLE_DIRECTORIES}" REAL_FILES VIRTUAL_PATHS)

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REAL_FILES})

  _bundle_stage_and_archive("${NAME}" "${BUNDLE_OUTPUT_DIR}" "${BUNDLE_COMPRESSION}" "${REAL_FILES}" "${VIRTUAL_PATHS}" PAK_FILE)

  _bundle_detect_os(OS)
  _bundle_generate_sources("${NAME}" "${OS}" "${PAK_FILE}" "${BUNDLE_OUTPUT_DIR}" ASM_FILE C_FILE HEADER_FILE)

  _bundle_ensure_runtime_target()

  add_library(${NAME} STATIC "${ASM_FILE}" "${C_FILE}")
  target_include_directories(${NAME} PUBLIC "${BUNDLE_OUTPUT_DIR}")
  target_link_libraries(${NAME} PUBLIC bundle_runtime)

  list(LENGTH VIRTUAL_PATHS FILE_COUNT)
  _bundle_log(STATUS "${NAME}: packed ${FILE_COUNT} file(s), ${BUNDLE_COMPRESSION} -> ${PAK_FILE}")
endfunction()
