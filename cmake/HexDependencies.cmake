set(HEXENGINE_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
set(HEXENGINE_CXXOPTS_INCLUDE_DIR "${HEXENGINE_THIRDPARTY_DIR}/cxxopts/include")
set(HEXENGINE_FASTNOISELITE_INCLUDE_DIR "${HEXENGINE_THIRDPARTY_DIR}/fastnoiselite/Cpp")
set(HEXENGINE_RAPIDXML_INCLUDE_DIR "${HEXENGINE_THIRDPARTY_DIR}/rapidxml")
set(HEXENGINE_NLOHMANN_JSON_INCLUDE_DIR "${HEXENGINE_THIRDPARTY_DIR}/rapidjson/include")
set(HEXENGINE_RECTPACK2D_INCLUDE_ROOT "${HEXENGINE_THIRDPARTY_DIR}/retpack2d/src")
set(HEXENGINE_RECTPACK2D_INCLUDE_DIR "${HEXENGINE_RECTPACK2D_INCLUDE_ROOT}/rectpack2D")

# Phase 3 starter: model cxxopts as a target-based external header dependency.
add_library(hex_dep_cxxopts INTERFACE)
add_library(Hex::cxxopts ALIAS hex_dep_cxxopts)
target_include_directories(hex_dep_cxxopts INTERFACE "${HEXENGINE_CXXOPTS_INCLUDE_DIR}")

add_library(hex_dep_fastnoiselite INTERFACE)
add_library(Hex::fastnoiselite ALIAS hex_dep_fastnoiselite)
target_include_directories(hex_dep_fastnoiselite INTERFACE "${HEXENGINE_FASTNOISELITE_INCLUDE_DIR}")

add_library(hex_dep_rapidxml INTERFACE)
add_library(Hex::rapidxml ALIAS hex_dep_rapidxml)
target_include_directories(hex_dep_rapidxml INTERFACE "${HEXENGINE_RAPIDXML_INCLUDE_DIR}")

add_library(hex_dep_nlohmann_json_headers INTERFACE)
add_library(Hex::nlohmann_json_headers ALIAS hex_dep_nlohmann_json_headers)
target_include_directories(hex_dep_nlohmann_json_headers INTERFACE "${HEXENGINE_NLOHMANN_JSON_INCLUDE_DIR}")

add_library(hex_dep_rectpack2d INTERFACE)
add_library(Hex::rectpack2d ALIAS hex_dep_rectpack2d)
target_include_directories(hex_dep_rectpack2d INTERFACE "${HEXENGINE_RECTPACK2D_INCLUDE_ROOT}" "${HEXENGINE_RECTPACK2D_INCLUDE_DIR}")

if(NOT EXISTS "${HEXENGINE_CXXOPTS_INCLUDE_DIR}")
    message(STATUS "Hex::cxxopts include directory missing. Run header-only bootstrap to fetch it.")
endif()

if(NOT EXISTS "${HEXENGINE_FASTNOISELITE_INCLUDE_DIR}")
    message(STATUS "Hex::fastnoiselite include directory missing. Run header-only bootstrap to fetch it.")
endif()

if(NOT EXISTS "${HEXENGINE_RAPIDXML_INCLUDE_DIR}")
    message(STATUS "Hex::rapidxml include directory missing. Run header-only bootstrap to fetch it.")
endif()

if(NOT EXISTS "${HEXENGINE_NLOHMANN_JSON_INCLUDE_DIR}")
    message(STATUS "Hex::nlohmann_json_headers include directory missing. Run header-only bootstrap to fetch it.")
endif()

if(NOT EXISTS "${HEXENGINE_RECTPACK2D_INCLUDE_DIR}" AND NOT EXISTS "${HEXENGINE_RECTPACK2D_INCLUDE_ROOT}")
    message(STATUS "Hex::rectpack2d include directory missing. Run header-only bootstrap to fetch it.")
endif()
