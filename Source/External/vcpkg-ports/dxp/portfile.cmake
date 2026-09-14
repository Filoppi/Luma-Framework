# Keep the SHA512 in sync with the released zip (bump together with the version).
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(DXP_ARCH "x86")
    set(DXP_SHA512 "b1335e775de2da845007be313a6bc308d6e28d622a7b9bd813cf4030c997feffb4aceedffb75fea99cceb7911df73e3c70a0dc21592b8814b95f3260b1f9ed32")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(DXP_ARCH "x64")
    set(DXP_SHA512 "cf7a69460272ba4b4f05cac6f73344ce2bf58c9127e5e9f834077df0bac213410b1286a3118dbe4f75ca54d87b975e06c997acae62996a348fe812a419ea65d1")
else()
    message(FATAL_ERROR "DXP supports only x64 and x86, got ${VCPKG_TARGET_ARCHITECTURE}")
endif()

vcpkg_download_distfile(DXP_ARCHIVE
    URLS "https://github.com/Izueh/DirectXShaderPatcher/releases/download/v${VERSION}/DirectXShaderPatcher-${VERSION}-ninja-${DXP_ARCH}.zip"
    FILENAME "dxp-${VERSION}-ninja-${DXP_ARCH}.zip"
    SHA512 ${DXP_SHA512}
)

vcpkg_extract_source_archive(DXP_SRC ARCHIVE "${DXP_ARCHIVE}" NO_REMOVE_ONE_LEVEL)

file(INSTALL "${DXP_SRC}/${DXP_ARCH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Release/lib/dxpatcher.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Debug/lib/dxpatcher.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
if(EXISTS "${DXP_SRC}/${DXP_ARCH}/Release/bin")
    file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Release/bin/dxcompiler.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Release/bin/ThirdPartyNotices.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/dxp/")
endif()
if(EXISTS "${DXP_SRC}/${DXP_ARCH}/Debug/bin")
    file(INSTALL "${DXP_SRC}/${DXP_ARCH}/Debug/bin/dxcompiler.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

file(WRITE "${CURRENT_PACKAGES_DIR}/share/dxp/copyright" "DirectXShaderPatcher: MIT License (see https://github.com/Izueh/DirectXShaderPatcher)\n\nBundled dxcompiler.dll (DirectXShaderCompiler) — see ThirdPartyNotices.txt.\n")

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
