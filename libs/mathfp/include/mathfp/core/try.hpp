#pragma once

#include <utility>

#include "mathfp/core/expected.hpp"

// Reason: C++ can't do early-return ergonomically.

#define MATHFP_DETAIL_CONCAT_INNER(a, b) a##b
#define MATHFP_DETAIL_CONCAT(a, b) MATHFP_DETAIL_CONCAT_INNER(a, b)
#define MATHFP_DETAIL_UNIQUE_NAME(base) MATHFP_DETAIL_CONCAT(base, __COUNTER__)

#define MATHFP_DETAIL_TRY_IMPL(res_name, expr)                                  \
  do {                                                                           \
    auto res_name = (expr);                                                      \
    if (!res_name) {                                                             \
      return ::mathfp::unexpected(std::move(res_name.error()));                  \
    }                                                                            \
  } while (0)

#define MATHFP_DETAIL_TRY_ASSIGN_IMPL(res_name, lhs, expr)                       \
  do {                                                                           \
    auto res_name = (expr);                                                      \
    if (!res_name) {                                                             \
      return ::mathfp::unexpected(std::move(res_name.error()));                  \
    }                                                                            \
    (lhs) = std::move(*res_name);                                                \
  } while (0)

#define MATHFP_DETAIL_TRY_LET_IMPL(res_name, type, name, expr)                   \
  auto res_name = (expr);                                                        \
  if (!res_name) {                                                               \
    return ::mathfp::unexpected(std::move(res_name.error()));                    \
  }                                                                              \
  type name = std::move(*res_name)

// Evaluate an Expected<...> expression; on error, return it from the current function.
#define MATHFP_TRY(expr) MATHFP_DETAIL_TRY_IMPL(MATHFP_DETAIL_UNIQUE_NAME(_mathfp_try_), (expr))

// Same, but also assign the value into lhs.
#define MATHFP_TRY_ASSIGN(lhs, expr)                                             \
  MATHFP_DETAIL_TRY_ASSIGN_IMPL(MATHFP_DETAIL_UNIQUE_NAME(_mathfp_try_), (lhs), (expr))

// Declare a variable and initialize it from an Expected<...> expression.
// Usage: MATHFP_TRY_LET(Type, name, expr)
#define MATHFP_TRY_LET(type, name, expr)                                         \
  MATHFP_DETAIL_TRY_LET_IMPL(MATHFP_DETAIL_UNIQUE_NAME(_mathfp_try_), type, name, (expr))
