#pragma once

#if !defined(MATHFP_HAS_LINALG) && !defined(MATHFP_HAS_INTEROP) && !defined(MATHFP_HAS_ALL)
#  error "mathfp/linalg requires Eigen. Enable linalg/interop features or provide eigen3."
#endif

#if defined(MATHFP_EIGEN_MPL2_ONLY) && !defined(EIGEN_MPL2_ONLY)
#  define EIGEN_MPL2_ONLY
#endif

#if defined(MATHFP_EIGEN_NO_DEBUG) && !defined(EIGEN_NO_DEBUG)
#  define EIGEN_NO_DEBUG
#endif

#if defined(MATHFP_EIGEN_DONT_ALIGN_STATICALLY) && !defined(EIGEN_DONT_ALIGN_STATICALLY)
#  define EIGEN_DONT_ALIGN_STATICALLY
#endif

#include <Eigen/Core>
#include <Eigen/SparseCore>
