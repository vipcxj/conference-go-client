vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/libuvc
    REF ea3c43a9a18524b4799f49250a2120c57917938c
    SHA512 9ce7d3a9959047c4f29b22e81b42b341bc70b1a82561b4c7791b48defb6235d524363a709d86f57e30495f1eeefc5365cc7e43ee558a67bec489b1f189f7b605
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