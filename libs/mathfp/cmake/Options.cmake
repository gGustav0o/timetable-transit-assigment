include_guard(GLOBAL)

option(MATHFP_ENABLE_RANGES  "Enable Boost.Range helpers (zip/combine etc.)" ON)
option(MATHFP_ENABLE_LINALG  "Enable Eigen-based linear algebra layer"       ON)
option(MATHFP_ENABLE_GRAPH   "Enable Boost.Graph layer"                      ON)
option(MATHFP_ENABLE_INTEROP "Enable graph<->Eigen interop helpers"          ON)

option(MATHFP_USE_FMT_HEADER_ONLY "Link against fmt::fmt-header-only instead of fmt::fmt" OFF)

option(MATHFP_EIGEN_MPL2_ONLY "Define EIGEN_MPL2_ONLY for consumers" OFF)
