#pragma once

#include <compare>

namespace mathfp {

struct Unit {
  constexpr Unit() noexcept                              = default;
  constexpr friend auto operator<=>(Unit, Unit) noexcept = default;
};

inline constexpr Unit kUnit{};

}
