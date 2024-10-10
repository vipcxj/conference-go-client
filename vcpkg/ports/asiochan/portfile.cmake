vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/asiochan
    REF ${VERSION}
    SHA512 7ee4290464e024b2bdca94e03a8fd6c0e37d9715edd8c8fb28d81627a7a4e568e319a10785a9e4393b42f720176ae510105bc24c4a4527f7cd465602af442401
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
