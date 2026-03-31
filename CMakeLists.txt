cmake_minimum_required(VERSION 3.24)

project(HexEngineBuild LANGUAGES NONE)

include(cmake/HexToolchain.cmake)
include(cmake/HexDependencies.cmake)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(HEXENGINE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(HEXENGINE_DEPENDENCY_MANIFEST "${HEXENGINE_REPO_ROOT}/build/dependencies.lock.json")

if(NOT EXISTS "${HEXENGINE_DEPENDENCY_MANIFEST}")
    message(FATAL_ERROR "Dependency manifest not found: ${HEXENGINE_DEPENDENCY_MANIFEST}")
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

add_custom_target(hex-legacy-setup
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --mode legacy
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Run legacy dependency bootstrap intentionally"
    VERBATIM
)

message(STATUS "HexEngine canonical orchestration entrypoint enabled.")
message(STATUS "Engine/editor/game compilation is still legacy MSBuild (.sln/.vcxproj) for this migration phase.")
