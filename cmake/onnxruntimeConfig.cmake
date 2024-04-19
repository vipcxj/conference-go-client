if(WIN32)
    set(onnxruntime_ROOT "${CMAKE_CURRENT_LIST_DIR}/../nuget/packages/Microsoft.ML.OnnxRuntime.Gpu.Windows")
elseif(UNIX)
    set(onnxruntime_ROOT "${CMAKE_CURRENT_LIST_DIR}/../nuget/packages/Microsoft.ML.OnnxRuntime.Gpu.Linux")
else()
    message(FATAL_ERROR "Nuget package only support Win and linux")
endif()

# Find onnxruntime library
#
# usage:
#   find_package(onnxruntime REQUIRED)
# if wants to check for particular execution provider:
#   find_package(onnxruntime REQUIRED COMPONENTS onnxruntime_providers_<PROVIDER_NAME>)
#   find_package(onnxruntime REQUIRED COMPONENTS onnxruntime_providers_cuda onnxruntime_providers_tensorrt)
# tested with cuda and tensorrt
#
#
# Note: This file only search libs of execution providers, not their dependencies 
#

include(FindPackageHandleStandardArgs)
# checking if onnxruntime_ROOT contain cmake find_package compatible config
file(GLOB __cmake_config_file "${onnxruntime_ROOT}/lib/cmake/onnxruntime/onnxruntimeConfig.cmake")
LIST(LENGTH __cmake_config_file __cmake_config_file_count)
if(__cmake_config_file_count GREATER 0)
  set(ARGS "")
  if(${onnxruntime_FIND_VERSION})
    list(APPEND ARGS ${onnxruntime_FIND_VERSION})
  endif()
  if(${onnxruntime_FIND_VERSION_EXACT})
    list(APPEND ARGS "EXACT")
  endif()
  if (${onnxruntime_FIND_QUIET})
    list(APPEND ARGS "QUIET")
  elseif (${onnxruntime_FIND_REQUIRED})
    list(APPEND ARGS "REQUIRED")
  endif()
  if(onnxruntime_FIND_COMPONENTS)
    list(APPEND ARGS "COMPONENTS" ${onnxruntime_FIND_COMPONENTS})
  endif()
  # recalling find_package with args but with specified path
  find_package(onnxruntime ${ARGS} CONFIG PATHS ${onnxruntime_ROOT})
else()
  # checking if onnxruntime_ROOT contain nuget package
  file(GLOB __nupkg_file "${onnxruntime_ROOT}/Microsoft.ML.OnnxRuntime.*.nupkg")
  LIST(LENGTH __nupkg_file __nuget_file_count)
  if(__nuget_file_count GREATER 0)
    list (GET __nupkg_file 0 __nupkg_file)
    STRING(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" onnxruntime_VERSION "${__nupkg_file}")
    message(STATUS "Loading using nuget")

    set(ORT_INCLUDE_DIR "${onnxruntime_ROOT}/buildTransitive/native/include/")
    if(WIN32)
      set(ORT_LIBRARY_DIR "${onnxruntime_ROOT}/runtimes/win-x64/native/")
    elseif(UNIX)
      set(ORT_LIBRARY_DIR "${onnxruntime_ROOT}/runtimes/linux-x64/native/")
    else()
      message(FATAL_ERROR "Nuget package only support Win and linux")
    endif()

    message("ORT_INCLUDE_DIR: ${ORT_INCLUDE_DIR}")

    set(onnxruntime_FOUND FALSE)
    if(EXISTS ${ORT_INCLUDE_DIR} AND EXISTS ${ORT_LIBRARY_DIR})
      set(onnxruntime_FOUND TRUE)
    endif()
    if(onnxruntime_FOUND)
      add_library(onnxruntime::onnxruntime SHARED IMPORTED)
      set_target_properties(onnxruntime::onnxruntime PROPERTIES 
        IMPORTED_CONFIGURATIONS RELEASE
        INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE_DIR}"
        )
      if(WIN32)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
          IMPORTED_IMPLIB "${ORT_LIBRARY_DIR}onnxruntime.lib"
          IMPORTED_LOCATION "${ORT_LIBRARY_DIR}onnxruntime.dll"
        )
      elseif(UNIX AND NOT APPLE)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_LOCATION "${ORT_LIBRARY_DIR}libonnxruntime.so")
      endif()
    endif()
  elseif(EXISTS "${onnxruntime_ROOT}/bin" AND EXISTS "${onnxruntime_ROOT}/include" AND EXISTS "${onnxruntime_ROOT}/lib")
    set(ORT_INCLUDE_DIR "${onnxruntime_ROOT}/include/")
    set(ORT_RUNTIME_DIR "${onnxruntime_ROOT}/bin/")
    set(ORT_LIBRARY_DIR "${onnxruntime_ROOT}/lib/")
    set(onnxruntime_FOUND TRUE)
    add_library(onnxruntime::onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES 
      IMPORTED_CONFIGURATIONS RELEASE
      INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE_DIR}"
      )
    if(WIN32)
      set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${ORT_LIBRARY_DIR}onnxruntime.lib"
        IMPORTED_LOCATION "${ORT_RUNTIME_DIR}onnxruntime.dll"
      )
    elseif(UNIX AND NOT APPLE)
      set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_LOCATION "${ORT_LIBRARY_DIR}libonnxruntime.so")
    endif()
  endif()
  if(onnxruntime_FOUND)
    function(import_providers provider_name)
      if(WIN32)
        set(lib_path "${ORT_LIBRARY_DIR}onnxruntime_providers_${provider_name}.dll")
      elseif(UNIX AND NOT APPLE)
        set(lib_path "${ORT_LIBRARY_DIR}libonnxruntime_providers_${provider_name}.so")
      endif()
      if(EXISTS ${lib_path})
          add_library(onnxruntime::onnxruntime_providers_${provider_name} MODULE IMPORTED)
          set_target_properties(onnxruntime::onnxruntime_providers_${provider_name} PROPERTIES 
            IMPORTED_CONFIGURATIONS RELEASE
            INTERFACE_INCLUDE_DIRECTORIES ${ORT_INCLUDE_DIR}
            IMPORTED_LOCATION ${lib_path}
          )
        set(onnxruntime_onnxruntime_providers_${provider_name}_FOUND TRUE PARENT_SCOPE)
      endif()
    endfunction(import_providers)
    set(ORT_PROVIDERS cuda;tensorrt)
    foreach(__component ${ORT_PROVIDERS})
      import_providers(${__component})
    endforeach()
    find_package_handle_standard_args(
      onnxruntime
      REQUIRED_VARS onnxruntime_ROOT 
      FOUND_VAR onnxruntime_FOUND
      VERSION_VAR onnxruntime_VERSION
      HANDLE_COMPONENTS
    )
  endif()
endif()