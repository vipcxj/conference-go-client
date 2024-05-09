vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO vipcxj/yamc
    REF v${VERSION}
    SHA512 65bbec2f3bcf62d03ad577c7a9098721d506e7671e16cd10a2402c468236408532e0f0f8dbc2efbfde5a6d2530990545d6cdb5d161ff5ccde6ba0a6bccc4d03c
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DENABLE_TESTING=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
