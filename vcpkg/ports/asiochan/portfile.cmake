vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/asiochan
    REF ${VERSION}
    SHA512 a5e11761034b74ff3b69665fbc9e624fadb8e16a503a177d77ce089b979f5e296554354ec7f83becaa841ee348d5101156b00cd13ec0c0a28efb01b532d8c7c4
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
