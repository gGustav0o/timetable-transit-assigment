#pragma once

// Version macros for preprocessor use (configured by CMake).
#define MATHFP_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define MATHFP_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define MATHFP_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define MATHFP_VERSION_STRING "@PROJECT_VERSION@"

// Packed version: 0xMMmmpp (major, minor, patch).
#define MATHFP_VERSION_HEX \
    ((MATHFP_VERSION_MAJOR << 16) | (MATHFP_VERSION_MINOR << 8) | MATHFP_VERSION_PATCH)

namespace mathfp {

	inline constexpr int kVersionMajor = MATHFP_VERSION_MAJOR;
	inline constexpr int kVersionMinor = MATHFP_VERSION_MINOR;
	inline constexpr int kVersionPatch = MATHFP_VERSION_PATCH;
	inline constexpr const char* kVersionString = MATHFP_VERSION_STRING;

}  // namespace mathfp
