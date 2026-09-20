# FindWDK.cmake — locate the Windows Driver Kit for the antidebug driver target.
#
# Sets:
#   WDK_FOUND            - kits root with a matching Include/Lib pair
#   WDK_VERSION          - selected kit version (e.g. 10.0.19041.0)
#   WDK_INCLUDE_DIR      - <kits>/Include/<ver>        (km/ subfolder per scope)
#   WDK_LIB_DIR          - <kits>/Lib/<ver>/km/x64
#
# Preference: WDK_VERSION cache entry, else 10.0.19041.0 (wave-1 target
# baseline 19045), else the newest installed kit.

set(WDK_VERSION "" CACHE STRING "Windows Kit version to use (e.g. 10.0.19041.0)")

function(_wdk_versions kits_root out_var)
  set(_found "")
  if(EXISTS "${kits_root}/Include")
    file(GLOB _dirs "${kits_root}/Include/10.0.*")
    list(SORT _dirs ORDER DESCENDING)
    foreach(_d IN LISTS _dirs)
      get_filename_component(_v "${_d}" NAME)
      if(EXISTS "${kits_root}/Lib/${_v}/km/x64")
        list(APPEND _found "${_v}")
      endif()
    endforeach()
  endif()
  set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

set(_kits_candidates
  "C:/Program Files (x86)/Windows Kits/10"
  "$ENV{ProgramFiles\(x86\)}/Windows Kits/10"
  "C:/Program Files/Windows Kits/10")

set(WDK_FOUND FALSE)
foreach(_kits IN LISTS _kits_candidates)
  _wdk_versions("${_kits}" _vers)
  if(NOT _vers)
    continue()
  endif()
  set(_pick "")
  if(WDK_VERSION)
    if(WDK_VERSION IN_LIST _vers)
      set(_pick "${WDK_VERSION}")
    endif()
  elseif("10.0.19041.0" IN_LIST _vers)
    set(_pick "10.0.19041.0")
  else()
    list(GET _vers 0 _pick) # sorted descending -> newest
  endif()
  if(NOT _pick)
    continue()
  endif()
  set(WDK_KITS "${_kits}" CACHE INTERNAL "WDK kits root")
  set(WDK_VERSION "${_pick}" CACHE STRING "Windows Kit version to use" FORCE)
  set(WDK_INCLUDE_DIR "${_kits}/Include/${_pick}" CACHE INTERNAL "WDK include root")
  set(WDK_LIB_DIR "${_kits}/Lib/${_pick}/km/x64" CACHE INTERNAL "WDK km x64 lib dir")
  set(WDK_FOUND TRUE)
  message(STATUS "FindWDK: using kit ${_pick} at ${_kits}")
  break()
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WDK DEFAULT_MSG WDK_FOUND WDK_INCLUDE_DIR WDK_LIB_DIR)
