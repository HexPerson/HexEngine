set(HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS
    "legacy_manifest"
    "fetchcontent"
    "vcpkg_manifest"
)

set(HEXENGINE_DEPENDENCY_BACKEND "legacy_manifest" CACHE STRING
    "Dependency backend for staged migration (legacy_manifest|fetchcontent|vcpkg_manifest).")
set_property(CACHE HEXENGINE_DEPENDENCY_BACKEND PROPERTY STRINGS ${HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS})

if(NOT HEXENGINE_DEPENDENCY_BACKEND IN_LIST HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS)
    message(FATAL_ERROR
        "Unsupported HEXENGINE_DEPENDENCY_BACKEND='${HEXENGINE_DEPENDENCY_BACKEND}'. "
        "Supported values: ${HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS}")
endif()

if(NOT HEXENGINE_DEPENDENCY_BACKEND STREQUAL "legacy_manifest")
    message(WARNING
        "HEXENGINE_DEPENDENCY_BACKEND='${HEXENGINE_DEPENDENCY_BACKEND}' is scaffolding-only in this phase. "
        "Canonical behavior still uses setup.py + build/dependencies.lock.json.")
endif()

function(hexengine_print_dependency_backend_summary)
    message(STATUS "HexEngine dependency backend: ${HEXENGINE_DEPENDENCY_BACKEND}")
    message(STATUS "Backends (scaffold): ${HEXENGINE_SUPPORTED_DEPENDENCY_BACKENDS}")
endfunction()
