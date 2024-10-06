vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/asiochan
    REF ${VERSION}
    SHA512 460970f8541f6ff1e2cd06d33dd4bfa0426026bb3346c6749d4d807d3fb2893f337e370b0752702636649bd6bccdb2a643d4e50d78cdb64032733039e750aadc
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
