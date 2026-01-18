#pragma once

#include <fmt/format.h>

#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <mathfp/compiler_attributes.hpp>

#include "mathfp/core/context.hpp"

namespace mathfp {

	enum class ErrKind : unsigned char {
		Domain
		, InvalidArg
		, NonConvergence
		, Singular
		, IllConditioned
		, Overflow
		, Underflow
		, PrecisionLoss
		, NotImplemented
		, Internal
	};

	MATHFP_NODISCARD constexpr std::string_view to_string(ErrKind k) noexcept {
		using enum ErrKind;
		switch (k) {
		case Domain:         return "Domain";
		case InvalidArg:     return "InvalidArg";
		case NonConvergence: return "NonConvergence";
		case Singular:       return "Singular";
		case IllConditioned: return "IllConditioned";
		case Overflow:       return "Overflow";
		case Underflow:      return "Underflow";
		case PrecisionLoss:  return "PrecisionLoss";
		case NotImplemented: return "NotImplemented";
		case Internal:       return "Internal";
		}
		return "Internal";
	}

	class Error {
		ErrKind kind_{ ErrKind::Internal };
		std::string message_;
		std::source_location where_ = std::source_location::current();
		Context context_;

	public:
		Error() = default;

		Error(
			ErrKind k
			, std::string msg
			, std::source_location loc = std::source_location::current()
		)
			: kind_(k)
			, message_(std::move(msg))
			, where_(loc)
			, context_()
		{}

		Error(
			ErrKind k
			, std::string msg
			, Context ctx
			, std::source_location loc = std::source_location::current()
		)
			: kind_(k)
			, message_(std::move(msg))
			, where_(loc)
			, context_(std::move(ctx))
		{}

		MATHFP_NODISCARD ErrKind kind() const noexcept { return kind_; }
		MATHFP_NODISCARD const std::string& message() const noexcept { return message_; }
		MATHFP_NODISCARD const Context& context() const noexcept { return context_; }
		MATHFP_NODISCARD const std::source_location& where() const noexcept { return where_; }

		template <class T>
		Error& ctx(std::string_view key, const T& value)& {
			context_.set(key, value);
			return *this;
		}

		template <class T>
		Error&& ctx(std::string_view key, const T& value)&& {
			context_.set(key, value);
			return std::move(*this);
		}

		Error& with_context(const Context& extra)& {
			context_.merge_from(extra);
			return *this;
		}

		Error&& with_context(const Context& extra)&& {
			context_.merge_from(extra);
			return std::move(*this);
		}

		Error& with_context(Context&& extra)& {
			context_.merge_from(std::move(extra));
			return *this;
		}

		Error&& with_context(Context&& extra)&& {
			context_.merge_from(std::move(extra));
			return std::move(*this);
		}

		MATHFP_NODISCARD std::string to_string() const {
			return fmt::format(
				"{}: {} ({}:{} in {}) {}"
				, mathfp::to_string(kind_)
				, message_
				, where_.file_name()
				, where_.line()
				, where_.function_name()
				, context_.to_string()
			);
		}

		Error& set_message(std::string msg) {
			message_ = std::move(msg);
			return *this;
		}

		Error& set_where(std::source_location where) noexcept {
			where_ = where;
			return *this;
		}

	};

	MATHFP_NODISCARD inline Error make_error(
		ErrKind kind
		, std::string message
		, std::source_location where = std::source_location::current()
	) {
		return Error{ kind, std::move(message), where };
	}

	MATHFP_NODISCARD inline Error domain_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Domain, msg, where);
	}

	MATHFP_NODISCARD inline Error invalid_arg(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::InvalidArg, msg, where);
	}

	MATHFP_NODISCARD inline Error non_convergence(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::NonConvergence, msg, where);
	}

	MATHFP_NODISCARD inline Error singular(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Singular, msg, where);
	}

	MATHFP_NODISCARD inline Error ill_conditioned(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::IllConditioned, msg, where);
	}

	MATHFP_NODISCARD inline Error overflow_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Overflow, msg, where);
	}

	MATHFP_NODISCARD inline Error underflow_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Underflow, msg, where);
	}

	MATHFP_NODISCARD inline Error precision_loss(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::PrecisionLoss, msg, where);
	}

	MATHFP_NODISCARD inline Error not_implemented(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::NotImplemented, msg, where);
	}

	MATHFP_NODISCARD inline Error internal_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Internal, msg, where);
	}


} // namespace mathfp


template <>
struct fmt::formatter<mathfp::ErrKind> : fmt::formatter<std::string_view> {
	template <class FormatContext>
	auto format(mathfp::ErrKind k, FormatContext& ctx) const {
		return fmt::formatter<std::string_view>::format(mathfp::to_string(k), ctx);
	}
};
