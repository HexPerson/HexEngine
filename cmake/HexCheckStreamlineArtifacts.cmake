if(NOT DEFINED HEXENGINE_STREAMLINE_LIB)
    message(FATAL_ERROR "HEXENGINE_STREAMLINE_LIB is not defined.")
endif()

set(_HEXENGINE_STREAMLINE_LIB "${HEXENGINE_STREAMLINE_LIB}")
string(REGEX REPLACE "^\"(.*)\"$" "\\1" _HEXENGINE_STREAMLINE_LIB "${_HEXENGINE_STREAMLINE_LIB}")

if(NOT EXISTS "${_HEXENGINE_STREAMLINE_LIB}")
    message(FATAL_ERROR
        "Streamline required library missing: ${_HEXENGINE_STREAMLINE_LIB}\n"
        "Run: cmake --build --preset required-modules-bootstrap-debug"
    )
endif()

file(READ "${_HEXENGINE_STREAMLINE_LIB}" HEXENGINE_STREAMLINE_HEAD LIMIT 80)
if(HEXENGINE_STREAMLINE_HEAD MATCHES "^version https://git-lfs.github.com/spec/v1")
    message(FATAL_ERROR
        "Streamline required library is an unresolved git-lfs pointer: ${_HEXENGINE_STREAMLINE_LIB}\n"
        "Install git-lfs, run 'git lfs install', then rerun required-modules bootstrap."
    )
endif()

message(STATUS "Streamline artifact check passed: ${_HEXENGINE_STREAMLINE_LIB}")
