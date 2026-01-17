#pragma once

#include <fmt/format.h>

#include <source_location>
#include <string>
#include <string_view>
#include <utility>

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

	[[nodiscard]] constexpr std::string_view to_string(ErrKind k) noexcept {
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
		ErrKind kind{ ErrKind::Internal };
		std::string message;
		std::source_location where = std::source_location::current();
		Context context;

	public:
		Error() = default;

		Error(
			ErrKind k
			, std::string msg
			, std::source_location loc = std::source_location::current()
		)
			: kind(k)
			, message(std::move(msg))
			, where(loc)
			, context()
		{}

		Error(
			ErrKind k
			, std::string msg
			, Context ctx
			, std::source_location loc = std::source_location::current()
		)
			: kind(k)
			, message(std::move(msg))
			, where(loc)
			, context(std::move(ctx))
		{}

		[[nodiscard]] ErrKind kind() const noexcept { return kind_; }
		[[nodiscard]] const std::string& message() const noexcept { return message_; }
		[[nodiscard]] const Context& context() const noexcept { return ctx_; }
		[[nodiscard]] const std::source_location& where() const noexcept { return where_; }

		template <class T>
		Error& ctx(std::string_view key, const T& value)& {
			context.set(key, value);
			return *this;
		}

		template <class T>
		Error&& ctx(std::string_view key, const T& value)&& {
			context.set(key, value);
			return std::move(*this);
		}

		Error& with_context(const Context& extra)& {
			context.merge_from(extra);
			return *this;
		}

		Error&& with_context(const Context& extra)&& {
			context.merge_from(extra);
			return std::move(*this);
		}

		Error& with_context(Context&& extra)& {
			context.merge_from(std::move(extra));
			return *this;
		}

		Error&& with_context(Context&& extra)&& {
			context.merge_from(std::move(extra));
			return std::move(*this);
		}

		[[nodiscard]] std::string to_string() const {
			return fmt::format(
				"{}: {} ({}:{} in {}) {}"
				, mathfp::to_string(kind)
				, message
				, where.file_name()
				, where.line()
				, where.function_name()
				, context.to_string()
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

	[[nodiscard]] inline Error make_error(
		ErrKind kind
		, std::string message
		, std::source_location where = std::source_location::current()
	) {
		return Error{ kind, std::move(message), where };
	}

	[[nodiscard]] inline Error domain_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Domain, msg, where);
	}

	[[nodiscard]] inline Error invalid_arg(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::InvalidArg, msg, where);
	}

	[[nodiscard]] inline Error non_convergence(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::NonConvergence, msg, where);
	}

	[[nodiscard]] inline Error singular(
		std::string_view msg
		, std::source_location where = std::source_location::current()\
	) {
		return make_error(ErrKind::Singular, msg, where);
	}

	[[nodiscard]] inline Error ill_conditioned(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::IllConditioned, msg, where);
	}

	[[nodiscard]] inline Error overflow_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()\
	) {
		return make_error(ErrKind::Overflow, msg, where);
	}

	[[nodiscard]] inline Error underflow_error(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::Underflow, msg, where);
	}

	[[nodiscard]] inline Error precision_loss(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::PrecisionLoss, msg, where);
	}

	[[nodiscard]] inline Error not_implemented(
		std::string_view msg
		, std::source_location where = std::source_location::current()
	) {
		return make_error(ErrKind::NotImplemented, msg, where);
	}

	[[nodiscard]] inline Error internal_error(
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