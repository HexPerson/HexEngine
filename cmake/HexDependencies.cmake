set(HEXENGINE_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
set(HEXENGINE_CXXOPTS_INCLUDE_DIR "${HEXENGINE_THIRDPARTY_DIR}/cxxopts/include")

# Phase 3 starter: model cxxopts as a target-based external header dependency.
add_library(hex_dep_cxxopts INTERFACE)
add_library(Hex::cxxopts ALIAS hex_dep_cxxopts)
target_include_directories(hex_dep_cxxopts INTERFACE "${HEXENGINE_CXXOPTS_INCLUDE_DIR}")

if(NOT EXISTS "${HEXENGINE_CXXOPTS_INCLUDE_DIR}")
    message(STATUS "Hex::cxxopts include directory missing. Run header-only bootstrap to fetch it.")
endif()
