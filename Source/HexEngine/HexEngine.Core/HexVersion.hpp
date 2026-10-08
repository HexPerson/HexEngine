// HexEngine version.
//
// The Git release tag (vMAJOR.MINOR.PATCH) is the single source of truth. The
// release workflow passes /p:HexVersion=MAJOR.MINOR.PATCH to MSBuild and
// Source/HexEngine/Directory.Build.targets turns that into the HEX_VERSION_*
// defines below for every C++ and resource (VERSIONINFO) compile - there is no
// version string to edit by hand before a release.
//
// A local build without /p:HexVersion gets 0.0.0 with HEX_VERSION_IS_DEV set.
// Game code compiled against an installed SDK reads the generated header below.
//
// Plain #defines only: this header is also included by the resource compiler
// (Source/HexEngine/Common/HexVersion.rc), which understands no C++.
#ifndef HEX_VERSION_HPP
#define HEX_VERSION_HPP

// Code built OUTSIDE the engine's MSBuild tree - a game project compiling
// against an installed SDK - gets the version from HexVersion.generated.h,
// which the release packaging writes into SDK\Include\HexEngine.Core\ from the
// same tag. (Not for the resource compiler, which has no __has_include.)
#if !defined(HEX_VERSION_MAJOR) && !defined(RC_INVOKED) && defined(__has_include)
#if __has_include("HexVersion.generated.h")
#include "HexVersion.generated.h"
#endif
#endif

#ifndef HEX_VERSION_MAJOR
#define HEX_VERSION_MAJOR 0
#define HEX_VERSION_MINOR 0
#define HEX_VERSION_PATCH 0
#ifndef HEX_VERSION_IS_DEV
#define HEX_VERSION_IS_DEV 1
#endif
#endif

#ifndef HEX_VERSION_IS_DEV
#define HEX_VERSION_IS_DEV 0
#endif

#define HEX_VERSION_STRINGIFY_IMPL(x) #x
#define HEX_VERSION_STRINGIFY(x) HEX_VERSION_STRINGIFY_IMPL(x)

// "MAJOR.MINOR.PATCH", e.g. "0.4.0".
#define HEX_VERSION_STRING \
	HEX_VERSION_STRINGIFY(HEX_VERSION_MAJOR) "." \
	HEX_VERSION_STRINGIFY(HEX_VERSION_MINOR) "." \
	HEX_VERSION_STRINGIFY(HEX_VERSION_PATCH)

#endif // HEX_VERSION_HPP
