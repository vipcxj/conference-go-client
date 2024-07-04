vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/asiochan
    REF ${VERSION}
    SHA512 d670597212e6888d190ef9d8aa80f7265c476680cafa904dbbefc9081ca69b2c540fde657e8ed246131d5583f6ab35d7cec73f2d707f20d3e799a566ee9f0346
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
