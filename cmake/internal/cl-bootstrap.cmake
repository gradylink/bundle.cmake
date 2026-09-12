# Fetches and includes Catalog (https://github.com/catalog-cmake/catalog) on
# demand - bundle_add() always resolves its DEFLATE backend (miniz or zlib)
# through it. This is a trimmed copy of Catalog's own cl-bootstrap.cmake,
# kept byte-compatible on its cache dir/guard variable so a project that
# already vendors cl-bootstrap.cmake (or includes catalog.cmake directly)
# shares the same cached copy instead of fetching a second one.
#
# Settings (as a CMake variable, or matching env var - variable wins; each
# accepts a CATALOG_BOOTSTRAP_ or CL_BOOTSTRAP_ prefix):
#   *_VERSION  release tag to fetch. Default: nightly
#   *_UPDATE   ALWAYS | NEVER | <seconds since last fetch before re-checking>. Default: 86400
#   *_MINIFIED ON/OFF - fetch catalog.min.cmake vs the readable catalog.cmake. Default: ON
#   *_URL      full override URL, e.g. for an internal mirror

if(DEFINED __CATALOG_BOOTSTRAPPED)
  return()
endif()
set(__CATALOG_BOOTSTRAPPED TRUE)

function(_bundle_clb_setting SUFFIX DEFAULT OUT_VAR)
  foreach(_PREFIX CATALOG_BOOTSTRAP_ CL_BOOTSTRAP_)
    if(DEFINED ${_PREFIX}${SUFFIX})
      set(${OUT_VAR} "${${_PREFIX}${SUFFIX}}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(_PREFIX CATALOG_BOOTSTRAP_ CL_BOOTSTRAP_)
    if(DEFINED ENV{${_PREFIX}${SUFFIX}} AND NOT "$ENV{${_PREFIX}${SUFFIX}}" STREQUAL "")
      set(${OUT_VAR} "$ENV{${_PREFIX}${SUFFIX}}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${OUT_VAR} "${DEFAULT}" PARENT_SCOPE)
endfunction()

_bundle_clb_setting(VERSION "nightly" _CLB_VERSION)
_bundle_clb_setting(UPDATE "86400" _CLB_UPDATE)
_bundle_clb_setting(MINIFIED "ON" _CLB_MINIFIED)
_bundle_clb_setting(URL "" _CLB_URL_OVERRIDE)

if(CMAKE_CROSSCOMPILING)
  set(_CLB_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cache/catalog")
elseif(WIN32)
  set(_CLB_CACHE_DIR "$ENV{LOCALAPPDATA}/catalog")
else()
  set(_CLB_CACHE_DIR "$ENV{HOME}/.cache/catalog")
endif()
set(_CLB_CACHE_DIR "${_CLB_CACHE_DIR}/bootstrap")
file(MAKE_DIRECTORY "${_CLB_CACHE_DIR}")

if(_CLB_MINIFIED)
  set(_CLB_ASSET "catalog.min.cmake")
else()
  set(_CLB_ASSET "catalog.cmake")
endif()

if(NOT "${_CLB_URL_OVERRIDE}" STREQUAL "")
  set(_CLB_URL "${_CLB_URL_OVERRIDE}")
else()
  set(_CLB_URL "https://github.com/catalog-cmake/catalog/releases/download/${_CLB_VERSION}/${_CLB_ASSET}")
endif()

set(_CLB_CACHED_FILE "${_CLB_CACHE_DIR}/${_CLB_VERSION}-${_CLB_ASSET}")
set(_CLB_STAMP_FILE "${_CLB_CACHED_FILE}.stamp")

set(_CLB_NEED_DOWNLOAD TRUE)
if(EXISTS "${_CLB_CACHED_FILE}")
  if(_CLB_UPDATE STREQUAL "NEVER")
    set(_CLB_NEED_DOWNLOAD FALSE)
  elseif(NOT _CLB_UPDATE STREQUAL "ALWAYS" AND EXISTS "${_CLB_STAMP_FILE}")
    file(TIMESTAMP "${_CLB_STAMP_FILE}" _CLB_STAMP_TIME "%s" UTC)
    string(TIMESTAMP _CLB_NOW "%s" UTC)
    math(EXPR _CLB_AGE "${_CLB_NOW} - ${_CLB_STAMP_TIME}")
    if(_CLB_AGE LESS "${_CLB_UPDATE}")
      set(_CLB_NEED_DOWNLOAD FALSE)
    endif()
  endif()
endif()

if(_CLB_NEED_DOWNLOAD)
  message(STATUS "[bundle] Fetching Catalog (${_CLB_VERSION})...")
  file(DOWNLOAD "${_CLB_URL}" "${_CLB_CACHED_FILE}.tmp" STATUS _CLB_DL_STATUS TIMEOUT 30)
  list(GET _CLB_DL_STATUS 0 _CLB_DL_CODE)
  if(_CLB_DL_CODE EQUAL 0)
    file(RENAME "${_CLB_CACHED_FILE}.tmp" "${_CLB_CACHED_FILE}")
    file(TOUCH "${_CLB_STAMP_FILE}")
  else()
    file(REMOVE "${_CLB_CACHED_FILE}.tmp")
    if(EXISTS "${_CLB_CACHED_FILE}")
      list(GET _CLB_DL_STATUS 1 _CLB_DL_ERR)
      message(WARNING "[bundle] Catalog update check failed (${_CLB_DL_ERR}), using cached copy")
    else()
      list(GET _CLB_DL_STATUS 1 _CLB_DL_ERR)
      message(FATAL_ERROR "[bundle] Failed to download Catalog from ${_CLB_URL}: ${_CLB_DL_ERR}")
    endif()
  endif()
endif()

include("${_CLB_CACHED_FILE}")
