#pragma once

#include <tl/expected.hpp>

#include <utility>

#include "mathfp/compiler_attributes.hpp"
#include "mathfp/core/error.hpp"
#include "mathfp/core/unit.hpp"

namespace mathfp {

    template <class T>
    using Expected = tl::expected<T, Error>;

    using Unexpected = tl::unexpected<Error>;

    MATHFP_NODISCARD inline Unexpected unexpected(Error e) {
        return Unexpected{ std::move(e) };
    }

    MATHFP_NODISCARD inline Expected<Unit> ok() { return Expected<Unit>{kUnit}; }

} // namespace mathfp
