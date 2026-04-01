cmake_minimum_required(VERSION 3.24)

project(HexEngineBuild LANGUAGES NONE)

include(cmake/HexToolchain.cmake)
include(cmake/HexDependencies.cmake)
include(cmake/HexLegacyImportedTargets.cmake)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(HEXENGINE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(HEXENGINE_DEPENDENCY_MANIFEST "${HEXENGINE_REPO_ROOT}/build/dependencies.lock.json")

if(NOT EXISTS "${HEXENGINE_DEPENDENCY_MANIFEST}")
    message(FATAL_ERROR "Dependency manifest not found: ${HEXENGINE_DEPENDENCY_MANIFEST}")
endif()

option(HEXENGINE_ENABLE_DEP_PROBE "Build a small CMake dependency probe target (Phase 3 validation)." OFF)
option(HEXENGINE_ENABLE_LEGACY_ASSIMP_PROBE "Build a small assimp imported-target probe executable." OFF)
option(HEXENGINE_ENABLE_LEGACY_BROTLI_PROBE "Build a small brotli imported-target probe executable." OFF)
option(HEXENGINE_ENABLE_PHYSX_PROBE "Build a small physx imported-target probe executable." OFF)
option(HEXENGINE_ENABLE_SHADERCONDUCTOR_PROBE "Build a small shaderconductor imported-target probe executable." OFF)
option(HEXENGINE_ENABLE_STREAMLINE_PROBE "Build a small streamline imported-target probe executable." OFF)

if(HEXENGINE_ENABLE_DEP_PROBE)
    enable_language(CXX)
    add_executable(hex-dep-probe EXCLUDE_FROM_ALL cmake/examples/dep_probe.cpp)
    target_compile_features(hex-dep-probe PRIVATE cxx_std_20)
    target_link_libraries(hex-dep-probe
        PRIVATE
            Hex::cxxopts
            Hex::fastnoiselite
            Hex::rapidxml
            Hex::nlohmann_json_headers
            Hex::rectpack2d
    )
endif()

if(HEXENGINE_ENABLE_LEGACY_ASSIMP_PROBE)
    enable_language(CXX)
    if(HEXENGINE_HAS_LEGACY_ASSIMP)
        add_executable(hex-assimp-probe EXCLUDE_FROM_ALL cmake/examples/assimp_probe.cpp)
        target_compile_features(hex-assimp-probe PRIVATE cxx_std_20)
        target_link_libraries(hex-assimp-probe PRIVATE Hex::assimp_legacy)
    else()
        message(WARNING "HEXENGINE_ENABLE_LEGACY_ASSIMP_PROBE=ON, but Hex::assimp_legacy is unavailable.")
        add_custom_target(hex-assimp-probe
            COMMAND "${CMAKE_COMMAND}" -E echo "hex-assimp-probe skipped: Hex::assimp_legacy is unavailable in this checkout."
            VERBATIM
        )
    endif()
endif()

if(HEXENGINE_ENABLE_LEGACY_BROTLI_PROBE)
    enable_language(CXX)
    if(HEXENGINE_HAS_LEGACY_BROTLI)
        add_executable(hex-brotli-probe EXCLUDE_FROM_ALL cmake/examples/brotli_probe.cpp)
        target_compile_features(hex-brotli-probe PRIVATE cxx_std_20)
        target_link_libraries(hex-brotli-probe PRIVATE Hex::brotli_legacy)
    else()
        message(WARNING "HEXENGINE_ENABLE_LEGACY_BROTLI_PROBE=ON, but Hex::brotli_legacy is unavailable.")
        add_custom_target(hex-brotli-probe
            COMMAND "${CMAKE_COMMAND}" -E echo "hex-brotli-probe skipped: Hex::brotli_legacy is unavailable in this checkout."
            VERBATIM
        )
    endif()
endif()

if(HEXENGINE_ENABLE_STREAMLINE_PROBE)
    enable_language(CXX)
    if(HEXENGINE_HAS_STREAMLINE_VENDOR)
        add_executable(hex-streamline-probe EXCLUDE_FROM_ALL cmake/examples/streamline_probe.cpp)
        target_compile_features(hex-streamline-probe PRIVATE cxx_std_20)
        target_link_libraries(hex-streamline-probe PRIVATE Hex::streamline_vendor)
    else()
        message(WARNING "HEXENGINE_ENABLE_STREAMLINE_PROBE=ON, but Hex::streamline_vendor is unavailable.")
        add_custom_target(hex-streamline-probe
            COMMAND "${CMAKE_COMMAND}" -E echo "hex-streamline-probe skipped: Hex::streamline_vendor is unavailable in this checkout."
            VERBATIM
        )
    endif()
endif()

if(HEXENGINE_ENABLE_SHADERCONDUCTOR_PROBE)
    enable_language(CXX)
    if(HEXENGINE_HAS_SHADERCONDUCTOR_VENDOR)
        add_executable(hex-shaderconductor-probe EXCLUDE_FROM_ALL cmake/examples/shaderconductor_probe.cpp)
        target_compile_features(hex-shaderconductor-probe PRIVATE cxx_std_20)
        target_link_libraries(hex-shaderconductor-probe PRIVATE Hex::shaderconductor_vendor)
    else()
        message(WARNING "HEXENGINE_ENABLE_SHADERCONDUCTOR_PROBE=ON, but Hex::shaderconductor_vendor is unavailable.")
        add_custom_target(hex-shaderconductor-probe
            COMMAND "${CMAKE_COMMAND}" -E echo "hex-shaderconductor-probe skipped: Hex::shaderconductor_vendor is unavailable in this checkout."
            VERBATIM
        )
    endif()
endif()

if(HEXENGINE_ENABLE_PHYSX_PROBE)
    enable_language(CXX)
    if(HEXENGINE_HAS_PHYSX_VENDOR)
        add_executable(hex-physx-probe EXCLUDE_FROM_ALL cmake/examples/physx_probe.cpp)
        target_compile_features(hex-physx-probe PRIVATE cxx_std_20)
        target_link_libraries(hex-physx-probe PRIVATE Hex::physx_vendor)
    else()
        message(WARNING "HEXENGINE_ENABLE_PHYSX_PROBE=ON, but Hex::physx_vendor is unavailable.")
        add_custom_target(hex-physx-probe
            COMMAND "${CMAKE_COMMAND}" -E echo "hex-physx-probe skipped: Hex::physx_vendor is unavailable in this checkout."
            VERBATIM
        )
    endif()
endif()

add_custom_target(hex-deps-plan
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --print-plan
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Print HexEngine dependency plan from build/dependencies.lock.json"
    VERBATIM
)

add_custom_target(hex-deps-check-refs
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --check-refs
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Check dependency ref pin status"
    VERBATIM
)

add_custom_target(hex-deps-check-refs-strict
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --check-refs-strict
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Fail when dependency refs are missing"
    VERBATIM
)

add_custom_target(hex-deps-lock-refs
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --lock-current-refs
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Lock dependency refs to local HEAD commits"
    VERBATIM
)

add_custom_target(hex-headeronly-bootstrap
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --mode legacy --header-only-bootstrap --header-layout external
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Fetch header-only dependencies (external-header layout; no full native builds)"
    VERBATIM
)

add_custom_target(hex-required-modules-bootstrap
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --mode legacy --frozen --required-modules-only
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Bootstrap required runtime modules only (streamline, physx, shaderconductor)"
    VERBATIM
)

add_custom_target(hex-legacy-setup
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --mode legacy --frozen
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Run legacy dependency bootstrap intentionally (pinned/frozen refs)"
    VERBATIM
)

message(STATUS "HexEngine canonical orchestration entrypoint enabled.")
message(STATUS "Engine/editor/game compilation is still legacy MSBuild (.sln/.vcxproj) for this migration phase.")
