cmake_minimum_required(VERSION 3.20)

project(mathfp
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/Options.cmake)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# tl::expected (package name: tl-expected, target: tl::expected)
find_package(tl-expected CONFIG REQUIRED)

# fmt (targets: fmt::fmt, fmt::fmt-header-only)
find_package(fmt CONFIG REQUIRED)
if(MATHFP_USE_FMT_HEADER_ONLY)
  set(_MATHFP_FMT_TARGET fmt::fmt-header-only)
else()
  set(_MATHFP_FMT_TARGET fmt::fmt)
endif()

# Boost (Graph + Range)
set(_MATHFP_BOOST_COMPONENTS "")
if(MATHFP_ENABLE_GRAPH)
  list(APPEND _MATHFP_BOOST_COMPONENTS graph)
endif()
if(MATHFP_ENABLE_RANGES)
  list(APPEND _MATHFP_BOOST_COMPONENTS range)
endif()

if(_MATHFP_BOOST_COMPONENTS)
  find_package(Boost REQUIRED COMPONENTS ${_MATHFP_BOOST_COMPONENTS})
endif()

# Eigen
if(MATHFP_ENABLE_LINALG OR MATHFP_ENABLE_INTEROP)
  find_package(Eigen3 3.3 REQUIRED NO_MODULE)
endif()

# ------------------------------------------------------------------------------
# Include root
# ------------------------------------------------------------------------------

set(MATHFP_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
set(MATHFP_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)

file(MAKE_DIRECTORY ${MATHFP_GENERATED_INCLUDE_DIR}/mathfp)

configure_file(
  ${MATHFP_INCLUDE_DIR}/mathfp/version.hpp
  ${MATHFP_GENERATED_INCLUDE_DIR}/mathfp/version.hpp
  @ONLY
)
set(MATHFP_HEADERS
  ${MATHFP_INCLUDE_DIR}/mathfp/config.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/compiler_attributes.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/prelude.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/version.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/expected.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/numeric_tolerance.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/summation.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/error.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/context.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/try.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/fp.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/applicative.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/traverse.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/unit.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/core/utility.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/ranges/boost.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/ranges/zip.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/types/strong_type.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/types/units.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/types/index.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/linalg/eigen_fwd.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/linalg/types.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/linalg/checks.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/linalg/sparse_build.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/linalg/solve.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/graph/types.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/graph/properties.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/graph/build.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/algorithms/bfs.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/algorithms/dijkstra.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/algorithms/toposort.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/algorithms/connected_components.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/interop/graph_eigen.hpp
  ${MATHFP_INCLUDE_DIR}/mathfp/interop/boost_property_maps.hpp
)

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

add_library(mathfp_core INTERFACE)
add_library(mathfp::core ALIAS mathfp_core)

target_sources(mathfp_core INTERFACE ${MATHFP_HEADERS})

target_include_directories(mathfp_core
  INTERFACE
    $<BUILD_INTERFACE:${MATHFP_GENERATED_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${MATHFP_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(mathfp_core INTERFACE cxx_std_20)

target_link_libraries(mathfp_core
  INTERFACE
    tl::expected
    ${_MATHFP_FMT_TARGET}
)

# Common compile definitions (kept minimal, since this is a library, not a cult)
target_compile_definitions(mathfp_core
  INTERFACE
    MATHFP_HAS_CORE=1
)

# ------------------------------------------------------------------------------
# Ranges module
# ------------------------------------------------------------------------------

if(MATHFP_ENABLE_RANGES)
  add_library(mathfp_ranges INTERFACE)
  add_library(mathfp::ranges ALIAS mathfp_ranges)

  target_include_directories(mathfp_ranges
    INTERFACE
      $<BUILD_INTERFACE:${MATHFP_GENERATED_INCLUDE_DIR}>
      $<BUILD_INTERFACE:${MATHFP_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )

  target_compile_features(mathfp_ranges INTERFACE cxx_std_20)

  target_link_libraries(mathfp_ranges
    INTERFACE
      mathfp_core
      Boost::range
  )

  target_compile_definitions(mathfp_ranges INTERFACE MATHFP_HAS_RANGES=1)
endif()

# ------------------------------------------------------------------------------
# Linalg module (Eigen)
# ------------------------------------------------------------------------------

if(MATHFP_ENABLE_LINALG)
  add_library(mathfp_linalg INTERFACE)
  add_library(mathfp::linalg ALIAS mathfp_linalg)

  target_include_directories(mathfp_linalg
    INTERFACE
      $<BUILD_INTERFACE:${MATHFP_GENERATED_INCLUDE_DIR}>
      $<BUILD_INTERFACE:${MATHFP_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )

  target_compile_features(mathfp_linalg INTERFACE cxx_std_20)

  target_link_libraries(mathfp_linalg
    INTERFACE
      mathfp_core
      Eigen3::Eigen
  )

  if(MATHFP_EIGEN_MPL2_ONLY)
    target_compile_definitions(mathfp_linalg INTERFACE EIGEN_MPL2_ONLY=1)
  endif()

  target_compile_definitions(mathfp_linalg INTERFACE MATHFP_HAS_LINALG=1)
endif()

# ------------------------------------------------------------------------------
# Graph module (Boost.Graph)
# ------------------------------------------------------------------------------

if(MATHFP_ENABLE_GRAPH)
  add_library(mathfp_graph INTERFACE)
  add_library(mathfp::graph ALIAS mathfp_graph)

  target_include_directories(mathfp_graph
    INTERFACE
      $<BUILD_INTERFACE:${MATHFP_GENERATED_INCLUDE_DIR}>
      $<BUILD_INTERFACE:${MATHFP_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )

  target_compile_features(mathfp_graph INTERFACE cxx_std_20)

  target_link_libraries(mathfp_graph
    INTERFACE
      mathfp_core
      Boost::graph
  )

  target_compile_definitions(mathfp_graph INTERFACE MATHFP_HAS_GRAPH=1)
endif()

# ------------------------------------------------------------------------------
# Interop module (Graph <-> Eigen)
# ------------------------------------------------------------------------------

if(MATHFP_ENABLE_INTEROP)
  add_library(mathfp_interop INTERFACE)
  add_library(mathfp::interop ALIAS mathfp_interop)

  target_include_directories(mathfp_interop
    INTERFACE
      $<BUILD_INTERFACE:${MATHFP_GENERATED_INCLUDE_DIR}>
      $<BUILD_INTERFACE:${MATHFP_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )

  target_compile_features(mathfp_interop INTERFACE cxx_std_20)

  target_link_libraries(mathfp_interop
    INTERFACE
      mathfp_core
      Eigen3::Eigen
  )

  if(MATHFP_ENABLE_LINALG)
    target_link_libraries(mathfp_interop INTERFACE mathfp_linalg)
  endif()

  if(MATHFP_ENABLE_GRAPH)
    target_link_libraries(mathfp_interop INTERFACE mathfp_graph)
  endif()

  target_compile_definitions(mathfp_interop INTERFACE MATHFP_HAS_INTEROP=1)
endif()

# ------------------------------------------------------------------------------
# Aggregate target
# ------------------------------------------------------------------------------

add_library(mathfp_all INTERFACE)
add_library(mathfp::all ALIAS mathfp_all)

target_include_directories(mathfp_all
  INTERFACE
    $<BUILD_INTERFACE:${MATHFP_GENERATED_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${MATHFP_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(mathfp_all INTERFACE cxx_std_20)
target_link_libraries(mathfp_all INTERFACE mathfp_core)

if(MATHFP_ENABLE_RANGES)
  target_link_libraries(mathfp_all INTERFACE mathfp_ranges)
endif()

if(MATHFP_ENABLE_LINALG)
  target_link_libraries(mathfp_all INTERFACE mathfp_linalg)
endif()

if(MATHFP_ENABLE_GRAPH)
  target_link_libraries(mathfp_all INTERFACE mathfp_graph)
endif()

if(MATHFP_ENABLE_INTEROP)
  target_link_libraries(mathfp_all INTERFACE mathfp_interop)
endif()

target_compile_definitions(mathfp_all INTERFACE MATHFP_HAS_ALL=1)
