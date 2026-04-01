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

set(HEXENGINE_PHYSX_ROOT "")
set(_hexengine_physx_layout_candidates
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/physx/physx"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/physx"
)
foreach(_hexengine_physx_candidate IN LISTS _hexengine_physx_layout_candidates)
    if(EXISTS "${_hexengine_physx_candidate}/include/PxPhysicsAPI.h")
        set(HEXENGINE_PHYSX_ROOT "${_hexengine_physx_candidate}")
        break()
    endif()
endforeach()

set(HEXENGINE_PHYSX_INCLUDE_DIR "${HEXENGINE_PHYSX_ROOT}/include")
set(HEXENGINE_PHYSX_PXSHARED_INCLUDE_DIR "${HEXENGINE_PHYSX_ROOT}/../pxshared/include")
set(HEXENGINE_PHYSX_DEBUG_LIB_DIR "${HEXENGINE_PHYSX_ROOT}/bin/win.x86_64.vc143.md/debug")
set(HEXENGINE_PHYSX_RELEASE_LIB_DIR "${HEXENGINE_PHYSX_ROOT}/bin/win.x86_64.vc143.md/release")
set(HEXENGINE_PHYSX_MAIN_DEBUG_LIB "${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysX_64.lib")
set(HEXENGINE_PHYSX_MAIN_RELEASE_LIB "${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysX_64.lib")

set(HEXENGINE_SHADERCONDUCTOR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/shaderconductor/Include")
set(HEXENGINE_SHADERCONDUCTOR_DEBUG_LIB "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/shaderconductor/Build/Lib/Debug/ShaderConductor.lib")
set(HEXENGINE_SHADERCONDUCTOR_RELEASE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/shaderconductor/Build/Lib/Release/ShaderConductor.lib")
set(HEXENGINE_STREAMLINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Streamline/include")
set(HEXENGINE_STREAMLINE_INTERPOSER_LIB "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Streamline/lib/x64/sl.interposer.lib")

set(HEXENGINE_HAS_LEGACY_ASSIMP OFF)
set(HEXENGINE_HAS_LEGACY_BROTLI OFF)
set(HEXENGINE_HAS_PHYSX_VENDOR OFF)
set(HEXENGINE_HAS_SHADERCONDUCTOR_VENDOR OFF)
set(HEXENGINE_HAS_STREAMLINE_VENDOR OFF)

function(hex_add_imported_static_target target_name alias_name debug_lib release_lib)
    add_library(${target_name} STATIC IMPORTED GLOBAL)
    add_library(${alias_name} ALIAS ${target_name})
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${debug_lib}"
        IMPORTED_LOCATION_RELEASE "${release_lib}"
    )
endfunction()

function(hex_add_imported_unknown_target target_name alias_name debug_lib release_lib)
    add_library(${target_name} UNKNOWN IMPORTED GLOBAL)
    add_library(${alias_name} ALIAS ${target_name})
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${debug_lib}"
        IMPORTED_LOCATION_RELEASE "${release_lib}"
    )
endfunction()

if(
    EXISTS "${HEXENGINE_ASSIMP_INCLUDE_DIR}"
    AND EXISTS "${HEXENGINE_ASSIMP_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_ASSIMP_RELEASE_LIB}"
    AND EXISTS "${HEXENGINE_ZLIB_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_ZLIB_RELEASE_LIB}"
)
    hex_add_imported_static_target(hex_legacy_zlib Hex::legacy_zlib "${HEXENGINE_ZLIB_DEBUG_LIB}" "${HEXENGINE_ZLIB_RELEASE_LIB}")

    hex_add_imported_static_target(hex_legacy_assimp Hex::assimp_legacy "${HEXENGINE_ASSIMP_DEBUG_LIB}" "${HEXENGINE_ASSIMP_RELEASE_LIB}")
    set_target_properties(hex_legacy_assimp PROPERTIES
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
    hex_add_imported_static_target(hex_legacy_brotlicommon Hex::legacy_brotlicommon "${HEXENGINE_BROTLICOMMON_DEBUG_LIB}" "${HEXENGINE_BROTLICOMMON_RELEASE_LIB}")
    set_target_properties(hex_legacy_brotlicommon PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_BROTLI_INCLUDE_DIR}")

    hex_add_imported_static_target(hex_legacy_brotlidec Hex::brotli_legacy "${HEXENGINE_BROTLIDEC_DEBUG_LIB}" "${HEXENGINE_BROTLIDEC_RELEASE_LIB}")
    set_target_properties(hex_legacy_brotlidec PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_BROTLI_INCLUDE_DIR}"
    )
    target_link_libraries(hex_legacy_brotlidec INTERFACE Hex::legacy_brotlicommon)

    set(HEXENGINE_HAS_LEGACY_BROTLI ON)
else()
    message(STATUS "Hex::brotli_legacy unavailable: expected staged legacy libs/includes were not found.")
endif()

if(
    EXISTS "${HEXENGINE_PHYSX_INCLUDE_DIR}"
    AND EXISTS "${HEXENGINE_PHYSX_MAIN_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_PHYSX_MAIN_RELEASE_LIB}"
)
    add_library(hex_vendor_physx INTERFACE)
    add_library(Hex::physx_vendor ALIAS hex_vendor_physx)
    target_include_directories(hex_vendor_physx INTERFACE "${HEXENGINE_PHYSX_INCLUDE_DIR}")
    if(EXISTS "${HEXENGINE_PHYSX_PXSHARED_INCLUDE_DIR}")
        target_include_directories(hex_vendor_physx INTERFACE "${HEXENGINE_PHYSX_PXSHARED_INCLUDE_DIR}")
    endif()
    target_link_libraries(hex_vendor_physx INTERFACE
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysX_64.lib>"
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysXFoundation_64.lib>"
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysXExtensions_static_64.lib>"
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysXCooking_64.lib>"
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysXPvdSDK_static_64.lib>"
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysXCharacterKinematic_static_64.lib>"
        "$<$<CONFIG:Debug>:${HEXENGINE_PHYSX_DEBUG_LIB_DIR}/PhysXCommon_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysX_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysXFoundation_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysXExtensions_static_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysXCooking_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysXPvdSDK_static_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysXCharacterKinematic_static_64.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${HEXENGINE_PHYSX_RELEASE_LIB_DIR}/PhysXCommon_64.lib>"
    )
    set(HEXENGINE_HAS_PHYSX_VENDOR ON)
else()
    message(STATUS "Hex::physx_vendor unavailable: expected PhysX headers/libs were not found in ThirdParty/physx.")
endif()

if(
    EXISTS "${HEXENGINE_SHADERCONDUCTOR_INCLUDE_DIR}"
    AND EXISTS "${HEXENGINE_SHADERCONDUCTOR_DEBUG_LIB}"
    AND EXISTS "${HEXENGINE_SHADERCONDUCTOR_RELEASE_LIB}"
)
    # Phase 3 migration: consume ShaderConductor directly from ThirdParty build output (no Libs/Include copy).
    hex_add_imported_unknown_target(
        hex_vendor_shaderconductor
        Hex::shaderconductor_vendor
        "${HEXENGINE_SHADERCONDUCTOR_DEBUG_LIB}"
        "${HEXENGINE_SHADERCONDUCTOR_RELEASE_LIB}"
    )
    set_target_properties(hex_vendor_shaderconductor PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_SHADERCONDUCTOR_INCLUDE_DIR}"
    )
    set(HEXENGINE_HAS_SHADERCONDUCTOR_VENDOR ON)
else()
    message(STATUS "Hex::shaderconductor_vendor unavailable: expected ThirdParty shaderconductor include/libs were not found.")
endif()

if(
    EXISTS "${HEXENGINE_STREAMLINE_INCLUDE_DIR}"
    AND EXISTS "${HEXENGINE_STREAMLINE_INTERPOSER_LIB}"
)
    add_library(hex_vendor_streamline UNKNOWN IMPORTED GLOBAL)
    add_library(Hex::streamline_vendor ALIAS hex_vendor_streamline)
    set_target_properties(hex_vendor_streamline PROPERTIES
        IMPORTED_LOCATION "${HEXENGINE_STREAMLINE_INTERPOSER_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${HEXENGINE_STREAMLINE_INCLUDE_DIR}"
    )
    set(HEXENGINE_HAS_STREAMLINE_VENDOR ON)
else()
    message(STATUS "Hex::streamline_vendor unavailable: expected Streamline include/lib artifacts were not found.")
endif()
