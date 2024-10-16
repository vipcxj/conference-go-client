vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/libuvc
    REF 3fbd9c48f4e5acfde75752bb714f5d1eeb32a458
    SHA512 c0c008d29f8911f6a5d2c1176a8d554775b91cf4cae16ddf55dd710c8829c648ed6dc75176a4e8ed5fe8d4bc797b6035e69a12033460e2ff74e9c61236e3371d
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

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")