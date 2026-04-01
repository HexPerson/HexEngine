set(HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS
    "legacy_manifest"
    "fetchcontent"
    "vcpkg_manifest"
)

set(HEXENGINE_DEPENDENCY_BACKEND "vcpkg_manifest" CACHE STRING
    "Dependency backend for staged migration (legacy_manifest|fetchcontent|vcpkg_manifest).")
set_property(CACHE HEXENGINE_DEPENDENCY_BACKEND PROPERTY STRINGS ${HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS})

if(NOT HEXENGINE_DEPENDENCY_BACKEND IN_LIST HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS)
    message(FATAL_ERROR
        "Unsupported HEXENGINE_DEPENDENCY_BACKEND='${HEXENGINE_DEPENDENCY_BACKEND}'. "
        "Supported values: ${HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS}")
endif()

if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(HEXENGINE_VCPKG_TOOLCHAIN_HINT "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
else()
    set(HEXENGINE_VCPKG_TOOLCHAIN_HINT "<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake")
endif()
set(HEXENGINE_VCPKG_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")
set(HEXENGINE_VCPKG_TOOLCHAIN_ACTIVE OFF)

if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
    set(HEXENGINE_VCPKG_TOOLCHAIN_ACTIVE ON)
endif()

if(HEXENGINE_DEPENDENCY_BACKEND STREQUAL "vcpkg_manifest")
    if(NOT EXISTS "${HEXENGINE_VCPKG_MANIFEST_PATH}")
        message(FATAL_ERROR "HEXENGINE_DEPENDENCY_BACKEND=vcpkg_manifest but manifest is missing: ${HEXENGINE_VCPKG_MANIFEST_PATH}")
    endif()
    if(NOT HEXENGINE_VCPKG_TOOLCHAIN_ACTIVE)
        message(WARNING
            "HEXENGINE_DEPENDENCY_BACKEND=vcpkg_manifest but vcpkg toolchain is not active. "
            "Set CMAKE_TOOLCHAIN_FILE=${HEXENGINE_VCPKG_TOOLCHAIN_HINT} (or use a vcpkg preset) to enable vcpkg package resolution.")
    endif()
endif()

function(hexengine_print_dependency_backend_summary)
    message(STATUS "HexEngine dependency backend: ${HEXENGINE_DEPENDENCY_BACKEND}")
    message(STATUS "Backends: ${HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS}")
    if(HEXENGINE_DEPENDENCY_BACKEND STREQUAL "vcpkg_manifest")
        message(STATUS "vcpkg manifest: ${HEXENGINE_VCPKG_MANIFEST_PATH}")
        message(STATUS "vcpkg toolchain active: ${HEXENGINE_VCPKG_TOOLCHAIN_ACTIVE}")
    endif()
endfunction()
