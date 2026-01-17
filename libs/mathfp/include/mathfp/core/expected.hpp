#pragma once

#include <tl/expected.hpp>

#include <utility>

#include "mathfp/core/error.hpp"
#include "mathfp/core/unit.hpp"

namespace mathfp {

	template <class T>
	using Expected = tl::expected<T, Error>;

	using Unexpected = tl::unexpected<Error>;

	[[nodiscard]] inline Unexpected unexpected(Error e) {
		return Unexpected{ std::move(e) };
	}

	[[nodiscard]] inline Expected<Unit> ok() { return Expected<Unit>{kUnit}; }

} // namespace mathfp
