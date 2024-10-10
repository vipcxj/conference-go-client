vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/asiochan
    REF ${VERSION}
    SHA512 13d87b8f10743de83a12f88fde29c4b4580acbd163ef8c102b18e9e84d36a3d5e05405734404bc604c31212bece85ca5bdab0b929c3b6fe2e049c82517ca6054
    HEAD_REF master
)

vcpkg_list(SET CMAKE_OPTIONS)
if("standalone-asio" IN_LIST FEATURES)
    if("boost-asio" IN_LIST FEATURES)
        message(FATAL "The feature standalone-asio is not compatibled with the feature boost-asio.")
    endif()
    list(APPEND CMAKE_OPTIONS -DASIOCHAN_USE_STANDALONE_ASIO=ON)
elseif("boost-asio" IN_LIST FEATURES)
    if("standalone-asio" IN_LIST FEATURES)
        message(FATAL "The feature standalone-asio is not compatibled with the feature boost-asio.")
    endif()
    list(APPEND CMAKE_OPTIONS -DASIOCHAN_USE_STANDALONE_ASIO=OFF)
else()
    message(FATAL "Either standalone-asio or boost-asio should be inside the feature list.")
endif()
if("ch-allocator-tracer" IN_LIST FEATURES)
    list(APPEND CMAKE_OPTIONS -DASIOCHAN_CH_ALLOCATE_TRACER=ON)
endif()
if("ch-allocator-tracer-full" IN_LIST FEATURES)
    list(APPEND CMAKE_OPTIONS -DASIOCHAN_CH_ALLOCATE_TRACER_FULL=ON)
endif()


vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${CMAKE_OPTIONS}
        -DENABLE_TESTING=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
