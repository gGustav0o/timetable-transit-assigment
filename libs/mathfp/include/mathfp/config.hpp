#pragma once

// Build feature flags (default to 0 when not provided by the build system).
#ifndef MATHFP_HAS_CORE
#  define MATHFP_HAS_CORE 0
#endif

#ifndef MATHFP_HAS_RANGES
#  define MATHFP_HAS_RANGES 0
#endif

#ifndef MATHFP_HAS_LINALG
#  define MATHFP_HAS_LINALG 0
#endif

#ifndef MATHFP_HAS_GRAPH
#  define MATHFP_HAS_GRAPH 0
#endif

#ifndef MATHFP_HAS_INTEROP
#  define MATHFP_HAS_INTEROP 0
#endif

#ifndef MATHFP_HAS_ALL
#  define MATHFP_HAS_ALL 0
#endif

// Optional configuration knobs (default to 0/off).
#ifndef MATHFP_USE_FMT_HEADER_ONLY
#  define MATHFP_USE_FMT_HEADER_ONLY 0
#endif

#ifndef MATHFP_EIGEN_MPL2_ONLY
#  define MATHFP_EIGEN_MPL2_ONLY 0
#endif
