vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO chmike/CxxUrl
    REF b45863c595ddfa68e75a1c95af17aa89ffc39268
    SHA512 09cf92211245ed740e2aa05e8cbbee74108190aa40c39f30bd75ebd69435d77ef0dc5b1a640e5aeff606fe62f985981bf8686052da03229423bbbd8a43d7ec77
    HEAD_REF master
    PATCHES
        fix-vcpkg.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CxxUrl)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")