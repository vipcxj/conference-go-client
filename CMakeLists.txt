cmake_minimum_required(VERSION 3.21)
project(conference-go-client)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# https://learn.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt?view=msvc-170
set(WIN_TARGET_VER 0x0601)

option(GSTREAMER_SUPPORT "gstreamer support")
option(USE_GSTREAMER_FULL_STATIC "use gstreamer full static" ON)
option(STANDALONE_ASIO "using standalone asio instead of boost-asio" OFF)
option(ENABLE_TEST "enable testing" ON)
if(GSTREAMER_SUPPORT)
    set(SUPPORT_GSTREAMER 1)
else()
    set(SUPPORT_GSTREAMER 0)
endif()

set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(H_IMPL "${SRC_PATH}/impl")
set(H_PUBLIC "${SRC_PATH}/public")
set(H_PUBLIC_PATH "${H_PUBLIC}/cfgo")

message(STATUS "configure file")
configure_file("${H_PUBLIC_PATH}/config/configuration.h.in" "${H_PUBLIC_PATH}/config/configuration.h" IMMEDIATE @ONLY)

message(STATUS "set src files")
set(MY_PUBLIC_HEADERS
    "${H_PUBLIC_PATH}/asio.hpp"
    "${H_PUBLIC_PATH}/config/configuration.h"
    "${H_PUBLIC_PATH}/configuration.hpp"
    "${H_PUBLIC_PATH}/client.hpp"
    "${H_PUBLIC_PATH}/common.hpp"
    "${H_PUBLIC_PATH}/subscribation.hpp"
    "${H_PUBLIC_PATH}/track.hpp"
    "${H_PUBLIC_PATH}/alias.hpp"
    "${H_PUBLIC_PATH}/coroutine_concepts.hpp"
    "${H_PUBLIC_PATH}/async.hpp"
    "${H_PUBLIC_PATH}/async_locker.hpp"
    "${H_PUBLIC_PATH}/basesink.hpp"
    "${H_PUBLIC_PATH}/defer.hpp"
    "${H_PUBLIC_PATH}/pattern.hpp"
    "${H_PUBLIC_PATH}/exports.h"
    "${H_PUBLIC_PATH}/fmt_helper.hpp"
    "${H_PUBLIC_PATH}/fmt.hpp"
    "${H_PUBLIC_PATH}/json.hpp"
    "${H_PUBLIC_PATH}/log.hpp"
    "${H_PUBLIC_PATH}/macros.h"
    "${H_PUBLIC_PATH}/move_only_function.hpp"
    "${H_PUBLIC_PATH}/black_magic.hpp"
    "${H_PUBLIC_PATH}/sio_helper.hpp"
    "${H_PUBLIC_PATH}/spd_helper.hpp"
    "${H_PUBLIC_PATH}/error.hpp"
    "${H_PUBLIC_PATH}/cbridge.hpp"
    "${H_PUBLIC_PATH}/capi.h"
    "${H_PUBLIC_PATH}/cfgo.hpp"
    "${H_PUBLIC_PATH}/utils.hpp"
)
set(MY_PRECOMPILE_HEADERS
    "${H_PUBLIC_PATH}/asio.hpp"
    "${H_PUBLIC_PATH}/fmt.hpp"
    "${H_PUBLIC_PATH}/json.hpp"
)
set(MY_GST_PLUGIN_PUBLIC_HEADERS
    "${H_PUBLIC_PATH}/gst/boxed.h"
    "${H_PUBLIC_PATH}/gst/buffer_pool.hpp"
    "${H_PUBLIC_PATH}/gst/gstcfgosrc.h"
    "${H_PUBLIC_PATH}/gst/error.h"
    "${H_PUBLIC_PATH}/gst/error.hpp"
    "${H_PUBLIC_PATH}/gst/pipeline.hpp"
    "${H_PUBLIC_PATH}/gst/appsink.hpp"
    "${H_PUBLIC_PATH}/gst/link.hpp"
    "${H_PUBLIC_PATH}/gst/gst.h"
    "${H_PUBLIC_PATH}/gst/gst.hpp"
    "${H_PUBLIC_PATH}/gst/helper.h"
    "${H_PUBLIC_PATH}/gst/utils.hpp"
)
set(H_PRIVATE "${SRC_PATH}/private")
set(H_PRIVATE_PATH "${H_PRIVATE}/cfgo")
set(MY_PRIVATE_HEADERS
    "${H_PRIVATE_PATH}/cofuture.hpp"
    "${H_PRIVATE_PATH}/cothread.hpp"
    "${H_PRIVATE_PATH}/coevent.hpp"
    "${H_PRIVATE_PATH}/rtc_helper.hpp"
    "${H_IMPL}/client.hpp"
    "${H_IMPL}/track.hpp"
    "${H_IMPL}/signal.hpp"
    "${H_IMPL}/subscribation.hpp"
)
set(MY_GST_PLUGIN_PRIVATE_HEADERS
    "${H_PRIVATE_PATH}/gst/cfgosrc.hpp"
    "${H_PRIVATE_PATH}/gst/gstcfgosrc_private_api.hpp"
    "${H_IMPL}/pipeline.hpp"
    "${H_IMPL}/link.hpp"
)
set(MY_SOURCES
    "${SRC_PATH}/configuration.cpp"
    "${SRC_PATH}/client.cpp"
    "${SRC_PATH}/track.cpp"
    "${SRC_PATH}/subscribation.cpp"
    "${SRC_PATH}/async.cpp"
    "${SRC_PATH}/async_locker.cpp"
    "${SRC_PATH}/basesink.cpp"
    "${SRC_PATH}/error.cpp"
    "${SRC_PATH}/log.cpp"
    "${SRC_PATH}/sio_helper.cpp"
    "${SRC_PATH}/rtc_helper.cpp"
    "${SRC_PATH}/coevent.cpp"
    "${SRC_PATH}/utils.cpp"
    "${SRC_PATH}/capi.cpp"
    "${H_IMPL}/client.cpp"
    "${H_IMPL}/track.cpp"
    "${H_IMPL}/signal.cpp"
    "${H_IMPL}/subscribation.cpp"
)
set(MY_GST_PLUGIN_SOURCES
    "${SRC_PATH}/gst/boxed.cpp"
    "${SRC_PATH}/gst/buffer_pool.cpp"
    "${SRC_PATH}/gst/gstcfgosrc.cpp"
    "${SRC_PATH}/gst/error.cpp"
    "${SRC_PATH}/gst/cfgosrc.cpp"
    "${SRC_PATH}/gst/pipeline.cpp"
    "${SRC_PATH}/gst/appsink.cpp"
    "${SRC_PATH}/gst/link.cpp"
    "${SRC_PATH}/gst/utils.cpp"
    "${SRC_PATH}/gst/helper.c"
    "${H_IMPL}/pipeline.cpp"
    "${H_IMPL}/link.cpp"
)
set(MY_TEST_PATH "${SRC_PATH}/test")

if(GSTREAMER_SUPPORT)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gstreamer.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/gstreamer.cmake"
        IMMEDIATE @ONLY)
    include("${CMAKE_CURRENT_BINARY_DIR}/cmake/gstreamer.cmake")
endif()

message(STATUS "find packages")
find_path(ASIOCHAN_INCLUDE_DIRS "asiochan/asiochan.hpp")
find_package(proxy CONFIG REQUIRED)
find_package(Boost REQUIRED)
#find_package(CxxUrl CONFIG REQUIRED)
find_package(cpptrace CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
#find_package(sioclient CONFIG REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yamc CONFIG REQUIRED)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

#list(APPEND CMAKE_INSTALL_RPATH [=[$ORIGIN/../lib]=])

function(link_gstreamer target is_private)
    if(USE_GSTREAMER_FULL_STATIC)
        set(to_links PkgConfig::gstreamer-full)
    else()
        set(to_links
            PkgConfig::gstreamer
            PkgConfig::gstreamer-allocators
            PkgConfig::gstreamer-app
            PkgConfig::gstreamer-audio
            PkgConfig::gstreamer-base
            PkgConfig::gstreamer-controller
            PkgConfig::gstreamer-net
            PkgConfig::gstreamer-pbutils
            PkgConfig::gstreamer-plugins-base
            PkgConfig::gstreamer-rtp
            PkgConfig::gstreamer-rtsp
            PkgConfig::gstreamer-tag
            PkgConfig::gstreamer-video
        )
    endif()
    if(is_private)
        target_link_libraries(${target} PRIVATE ${to_links})
        target_compile_definitions(${target} PRIVATE GST_USE_UNSTABLE_API)
    else()
        target_link_libraries(${target} PUBLIC ${to_links})
        target_compile_definitions(${target} PUBLIC GST_USE_UNSTABLE_API)
    endif()
endfunction(link_gstreamer target is_private)

function(link_common_libs target)
    target_link_libraries(${target} PUBLIC cpptrace::cpptrace)
    target_compile_definitions(${target} PUBLIC SPDLOG_FMT_EXTERNAL)
    target_link_libraries(${target} PUBLIC spdlog::spdlog)
    target_link_libraries(${target} PUBLIC fmt::fmt)
endfunction(link_common_libs target)

function(include_asiochan target)
    if(STANDALONE_ASIO)
        target_compile_definitions(${target} PUBLIC ASIOCHAN_USE_STANDALONE_ASIO)
    endif()
    target_include_directories(${target} PRIVATE ${ASIOCHAN_INCLUDE_DIRS})
endfunction(include_asiochan target)

function(fix_win_version_warn target)
    if(WIN32)
        message("WIN_TARGET_VER=${WIN_TARGET_VER}")
        target_compile_definitions(${target} PRIVATE _WIN32_WINNT=${WIN_TARGET_VER})
        target_compile_options(${target} PRIVATE "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
        target_compile_options(${target} PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    endif()
endfunction()

message(STATUS "add library cfgoclient")
add_library(cfgoclient ${MY_SOURCES} ${MY_PUBLIC_HEADERS} ${MY_PRIVATE_HEADERS})
target_compile_definitions(cfgoclient PRIVATE BUILDING_CFGO)
target_precompile_headers(cfgoclient PRIVATE ${MY_PRECOMPILE_HEADERS})
target_include_directories(cfgoclient PUBLIC
    $<BUILD_INTERFACE:${H_PUBLIC}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(cfgoclient PRIVATE ${H_PRIVATE} ${SRC_PATH})
target_link_libraries(cfgoclient PUBLIC yamc)
target_link_libraries(cfgoclient PRIVATE msft_proxy)
#target_link_libraries(cfgoclient PRIVATE chmike::CxxUrl)
fix_win_version_warn(cfgoclient)
link_common_libs(cfgoclient)
include_asiochan(cfgoclient)
target_include_directories(cfgoclient PRIVATE ${Boost_INCLUDE_DIRS})
#target_link_libraries(cfgoclient PRIVATE sioclient::sioclient sioclient::sioclient_tls)
target_link_libraries(cfgoclient PRIVATE nlohmann_json::nlohmann_json)
if(TARGET LibDataChannel::LibDataChannel)
    target_link_libraries(cfgoclient PRIVATE LibDataChannel::LibDataChannel)
else()
    target_link_libraries(cfgoclient PRIVATE LibDataChannel::LibDataChannelStatic)
endif()
if(GSTREAMER_SUPPORT)
    link_gstreamer(cfgoclient true)
endif()

if(GSTREAMER_SUPPORT)
    message(STATUS "add library cfgogst")
    add_library(cfgogst ${MY_GST_PLUGIN_SOURCES} ${MY_GST_PLUGIN_PUBLIC_HEADERS} ${MY_GST_PLUGIN_PRIVATE_HEADERS})
    target_compile_definitions(cfgogst PRIVATE BUILDING_CFGO)
    target_include_directories(cfgogst PUBLIC
        $<BUILD_INTERFACE:${H_PUBLIC}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_include_directories(cfgogst PRIVATE ${H_PRIVATE} ${SRC_PATH})
    target_include_directories(cfgogst PRIVATE ${Boost_INCLUDE_DIRS})
    fix_win_version_warn(cfgogst)
    target_link_libraries(cfgogst PUBLIC cfgoclient)
    include_asiochan(cfgogst)
    link_gstreamer(cfgogst false)
endif()

if(ENABLE_TEST)
    include(CTest)

    find_package(GTest CONFIG REQUIRED)

    if(LINUX)
        pkg_check_modules(libunwind REQUIRED IMPORTED_TARGET GLOBAL libunwind)
    endif()

    add_executable(test-coevent "${MY_TEST_PATH}/coevent.cpp" "${SRC_PATH}/coevent.cpp" "${H_PRIVATE_PATH}/coevent.hpp")
    target_include_directories(test-coevent PRIVATE "${H_PUBLIC}")
    target_include_directories(test-coevent PRIVATE "${H_PRIVATE}")
    #find_package(asio CONFIG REQUIRED)
    #target_link_libraries(test-coevent PRIVATE asio::asio)
    target_link_libraries(test-coevent PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    add_test(NAME test-coevent COMMAND test-coevent)

    add_executable(test-async "${MY_TEST_PATH}/async.cpp")
    include_asiochan(test-async)
    fix_win_version_warn(test-async)
    target_link_libraries(test-async PRIVATE cfgoclient)
    target_link_libraries(test-async PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    add_test(NAME test-async COMMAND test-async)

    if(GSTREAMER_SUPPORT)
        find_package(CUDAToolkit)
        find_package(cuda-api-wrappers CONFIG REQUIRED)
        #find_package(OpenCV CONFIG REQUIRED)

        subdirs(${SRC_PATH}/test/cuda)


        message(STATUS "nuget configure")
        include("${CMAKE_CURRENT_LIST_DIR}/scripts/cmake/NuGetTools/NuGetTools.cmake")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nuget/packages")
        set(NUGET_PACKAGES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nuget/packages)
        nuget_initialize()
        set(ONNX_VERSION "1.17.1")
        nuget_add_dependencies(
            PACKAGE Microsoft.ML.OnnxRuntime.Gpu VERSION ${ONNX_VERSION} CMAKE_PREFIX_PATHS "../../../cmake"
        )
        if(UNIX)
            if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nuget/packages/Microsoft.ML.OnnxRuntime.Gpu.Linux/runtimes/linux-x64/native/libonnxruntime.so.${ONNX_VERSION}")
                execute_process(
                    COMMAND ${CMAKE_COMMAND} -E create_symlink libonnxruntime.so "libonnxruntime.so.${ONNX_VERSION}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nuget/packages/Microsoft.ML.OnnxRuntime.Gpu.Linux/runtimes/linux-x64/native/"
                )
            endif()
        endif()

        find_package(onnxruntime CONFIG REQUIRED COMPONENTS onnxruntime_providers_cuda OPTIONAL_COMPONENTS onnxruntime_providers_tensorrt)
        pkg_search_module(gstreamer-cuda REQUIRED IMPORTED_TARGET gstreamer-cuda-1.0)

        add_executable(test-gst "${MY_TEST_PATH}/gst.cpp")
        target_compile_definitions(test-gst PRIVATE BUILDING_CFGO)
        target_include_directories(test-gst PRIVATE ${H_PUBLIC} ${H_PRIVATE} ${SRC_PATH} ${MY_TEST_PATH})
        target_link_libraries(test-gst PRIVATE cfgoclient cfgogst)
        target_link_libraries(test-gst PRIVATE CUDA::cudart)
        target_link_libraries(test-gst PRIVATE cfgo-cuda)
        fix_win_version_warn(test-gst)
        # link_gstreamer(test-gst true)
        target_link_libraries(test-gst PRIVATE PkgConfig::gstreamer-cuda)
        include_asiochan(test-gst)
        find_package(Poco REQUIRED COMPONENTS Net Foundation)
        target_link_libraries(test-gst PRIVATE Poco::Foundation Poco::Net)
        #target_link_libraries(test-gst PRIVATE opencv_ml opencv_dnn opencv_dpm opencv_hdf)
        target_link_libraries(test-gst PRIVATE cuda-api-wrappers::runtime-and-driver)
        target_link_libraries(test-gst PRIVATE onnxruntime::onnxruntime)
        if(WIN32)
            add_custom_command(TARGET test-gst POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/bin
                $<TARGET_FILE_DIR:test-gst>
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_LIST_DIR}/nuget/packages/Microsoft.ML.OnnxRuntime.Gpu.Windows/runtimes/win-x64/native
                $<TARGET_FILE_DIR:test-gst>
            )
        else()
            target_link_libraries(test-gst PRIVATE PkgConfig::libunwind)
        endif()

        add_executable(test-gstreamer "${MY_TEST_PATH}/gstreamer.cpp")
        target_include_directories(test-gstreamer PUBLIC ${H_PUBLIC})
        target_include_directories(test-gstreamer PRIVATE ${H_PRIVATE})
        include_asiochan(test-gstreamer)
        link_gstreamer(test-gstreamer true)
        find_package(Poco REQUIRED COMPONENTS Net Foundation)
        target_link_libraries(test-gstreamer PRIVATE Poco::Foundation Poco::Net)
        target_link_libraries(test-gstreamer PRIVATE cfgoclient cfgogst)
    endif()
endif()

message(STATUS "configure install")
if(GSTREAMER_SUPPORT)
    set(INSTALL_TARGETS cfgoclient cfgogst)
else()
    set(INSTALL_TARGETS cfgoclient)
endif()
install(TARGETS ${INSTALL_TARGETS} EXPORT ${CMAKE_PROJECT_NAME}-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/src/public/cfgo" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    REGEX "/gst" EXCLUDE
    REGEX "/gst/.*" EXCLUDE
)
if(GSTREAMER_SUPPORT)
    install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/src/public/cfgo/gst" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cfgo")
endif()

# Export targets
install(
	EXPORT ${CMAKE_PROJECT_NAME}-targets
	FILE ${CMAKE_PROJECT_NAME}-targets.cmake
	NAMESPACE ${CMAKE_PROJECT_NAME}::
	DESTINATION share/${CMAKE_PROJECT_NAME}
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cmake/gstreamer.cmake" DESTINATION share/${CMAKE_PROJECT_NAME})
if(GSTREAMER_SUPPORT)
    set(FIND_GSTREAMER "include(\"\${CMAKE_CURRENT_LIST_DIR}/gstreamer.cmake\")\n")
else()
    set(FIND_GSTREAMER)
endif()

# Generate the config file in the current binary dir (this ensures it's not placed directly in source)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
    "include(CMakeFindDependencyMacro)\n"
    "find_dependency(cpptrace CONFIG REQUIRED)\n"
    "find_dependency(spdlog CONFIG REQUIRED)\n"
    "find_dependency(fmt CONFIG REQUIRED)\n"
    #"find_dependency(sioclient CONFIG REQUIRED)\n"
    "find_dependency(LibDataChannel CONFIG REQUIRED)\n"
    "find_dependency(nlohmann_json CONFIG REQUIRED)\n"
    "find_dependency(yamc CONFIG REQUIRED)\n"
    "${FIND_GSTREAMER}"
    "if(NOT TARGET ${CMAKE_PROJECT_NAME}::${CMAKE_PROJECT_NAME})\n"
    "    include(\"\${CMAKE_CURRENT_LIST_DIR}/${CMAKE_PROJECT_NAME}-targets.cmake\")\n"
    "endif()"
)
# Install the generated config file
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
        DESTINATION share/${CMAKE_PROJECT_NAME})

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake/uninstall.cmake)
endif()
message(STATUS "configure end")