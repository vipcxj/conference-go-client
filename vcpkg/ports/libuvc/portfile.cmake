vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/libuvc
    REF df8001fc2b280c6105515efe7620363f2cdcad3c
    SHA512 bacbad91baa027d6278cdd704844997ea0df76f565c7be3183a483e7da3f8e9b929ebe4787026dd69c1af7ca5405761a516965164b9cc49748869a7807a90e77
    HEAD_REF master
)

if (VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(BUILD_TARGET "Shared")
else()
    set(BUILD_TARGET "Static")
endif()

vcpkg_find_acquire_program(PKGCONFIG)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DPKG_CONFIG_EXECUTABLE="${PKGCONFIG}"
        -DCMAKE_BUILD_TARGET=${BUILD_TARGET}
        -DBUILD_EXAMPLE=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/${PORT})
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")