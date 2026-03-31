cmake_minimum_required(VERSION 3.24)

project(HexEngineBuild LANGUAGES NONE)

include(cmake/HexToolchain.cmake)

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

add_custom_target(hex-legacy-setup
    COMMAND "${Python3_EXECUTABLE}" "${HEXENGINE_REPO_ROOT}/setup.py" --mode legacy
    WORKING_DIRECTORY "${HEXENGINE_REPO_ROOT}"
    COMMENT "Run legacy dependency bootstrap intentionally"
    VERBATIM
)

message(STATUS "HexEngine canonical orchestration entrypoint enabled.")
message(STATUS "Engine/editor/game compilation is still legacy MSBuild (.sln/.vcxproj) for this migration phase.")
