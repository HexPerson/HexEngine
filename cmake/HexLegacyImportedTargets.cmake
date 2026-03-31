set(HEXENGINE_LEGACY_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libs/x64")
set(HEXENGINE_ASSIMP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/assimp/include")

set(HEXENGINE_ASSIMP_DEBUG_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Debug/assimp-vc143-mtd.lib")
set(HEXENGINE_ASSIMP_RELEASE_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Release/assimp-vc143-mt.lib")
set(HEXENGINE_ZLIB_DEBUG_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Debug/zlibstaticd.lib")
set(HEXENGINE_ZLIB_RELEASE_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Release/zlibstatic.lib")
set(HEXENGINE_BROTLI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/brotli/c/include")
set(HEXENGINE_BROTLICOMMON_DEBUG_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Debug/brotlicommon.lib")
set(HEXENGINE_BROTLICOMMON_RELEASE_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Release/brotlicommon.lib")
set(HEXENGINE_BROTLIDEC_DEBUG_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Debug/brotlidec.lib")
set(HEXENGINE_BROTLIDEC_RELEASE_LIB "${HEXENGINE_LEGACY_LIB_DIR}/Release/brotlidec.lib")

set(HEXENGINE_HAS_LEGACY_ASSIMP OFF)
set(HEXENGINE_HAS_LEGACY_BROTLI OFF)

if(
    EXISTS "${HEXENGINE_ASSIMP_INCLUDE_DIR}"
    AND EXISTS "${HEXENGINE_ASSIMP_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_ASSIMP_RELEASE_LIB}"
    AND EXISTS "${HEXENGINE_ZLIB_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_ZLIB_RELEASE_LIB}"
)
    add_library(hex_legacy_zlib STATIC IMPORTED GLOBAL)
    add_library(Hex::legacy_zlib ALIAS hex_legacy_zlib)
    set_target_properties(hex_legacy_zlib PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${HEXENGINE_ZLIB_DEBUG_LIB}"
        IMPORTED_LOCATION_RELEASE "${HEXENGINE_ZLIB_RELEASE_LIB}"
    )

    add_library(hex_legacy_assimp STATIC IMPORTED GLOBAL)
    add_library(Hex::assimp_legacy ALIAS hex_legacy_assimp)
    set_target_properties(hex_legacy_assimp PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${HEXENGINE_ASSIMP_DEBUG_LIB}"
        IMPORTED_LOCATION_RELEASE "${HEXENGINE_ASSIMP_RELEASE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_ASSIMP_INCLUDE_DIR}"
    )
    target_link_libraries(hex_legacy_assimp INTERFACE Hex::legacy_zlib)

    set(HEXENGINE_HAS_LEGACY_ASSIMP ON)
else()
    message(STATUS "Hex::assimp_legacy unavailable: expected staged legacy libs/includes were not found.")
endif()

if(
    EXISTS "${HEXENGINE_BROTLI_INCLUDE_DIR}"
    AND EXISTS "${HEXENGINE_BROTLICOMMON_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_BROTLICOMMON_RELEASE_LIB}"
    AND EXISTS "${HEXENGINE_BROTLIDEC_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_BROTLIDEC_RELEASE_LIB}"
)
    add_library(hex_legacy_brotlicommon STATIC IMPORTED GLOBAL)
    add_library(Hex::legacy_brotlicommon ALIAS hex_legacy_brotlicommon)
    set_target_properties(hex_legacy_brotlicommon PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${HEXENGINE_BROTLICOMMON_DEBUG_LIB}"
        IMPORTED_LOCATION_RELEASE "${HEXENGINE_BROTLICOMMON_RELEASE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_BROTLI_INCLUDE_DIR}"
    )

    add_library(hex_legacy_brotlidec STATIC IMPORTED GLOBAL)
    add_library(Hex::brotli_legacy ALIAS hex_legacy_brotlidec)
    set_target_properties(hex_legacy_brotlidec PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${HEXENGINE_BROTLIDEC_DEBUG_LIB}"
        IMPORTED_LOCATION_RELEASE "${HEXENGINE_BROTLIDEC_RELEASE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_BROTLI_INCLUDE_DIR}"
    )
    target_link_libraries(hex_legacy_brotlidec INTERFACE Hex::legacy_brotlicommon)

    set(HEXENGINE_HAS_LEGACY_BROTLI ON)
else()
    message(STATUS "Hex::brotli_legacy unavailable: expected staged legacy libs/includes were not found.")
endif()
