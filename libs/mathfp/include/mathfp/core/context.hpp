#pragma once

#include <fmt/format.h>

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mathfp/compiler_attributes.hpp>

namespace mathfp {

	namespace ctx_key {
		inline constexpr std::string_view kRows     = "rows";
		inline constexpr std::string_view kCols     = "cols";
		inline constexpr std::string_view kN        = "n";
		inline constexpr std::string_view kM        = "m";
		inline constexpr std::string_view kEps      = "eps";
		inline constexpr std::string_view kTol      = "tol";
		inline constexpr std::string_view kIter     = "iter";
		inline constexpr std::string_view kMaxIter  = "max_iter";
		inline constexpr std::string_view kResidual = "residual";
		inline constexpr std::string_view kRank     = "rank";
	}

	class Context {
	public:
		struct Entry {
			std::string key;
			std::string value;
		};

		Context() = default;

		explicit Context(std::initializer_list<Entry> init)
			: entries_(init.begin(), init.end()) {
		}

		MATHFP_NODISCARD bool empty() const noexcept { return entries_.empty(); }
		MATHFP_NODISCARD std::size_t size() const noexcept { return entries_.size(); }

		MATHFP_NODISCARD std::span<const Entry> entries() const noexcept { return entries_; }

		void reserve(std::size_t n) { entries_.reserve(n); }
		void clear() noexcept { entries_.clear(); }

		template <class T>
		void set(std::string_view key, const T& v) {
			set_formatted(key, fmt::format("{}", v));
		}

		void set_formatted(std::string_view key, std::string value) {
			for (auto& e : entries_) {
				if (e.key == key) {
					e.value = std::move(value);
					return;
				}
			}
			entries_.push_back(Entry{ std::string(key), std::move(value) });
		}

		void merge_from(const Context& other) {
			for (const auto& e : other.entries_) {
				set_formatted(e.key, e.value);
			}
		}

		void merge_from(Context&& other) {
			for (auto& e : other.entries_) {
				set_formatted(e.key, std::move(e.value));
			}
			other.entries_.clear();
		}

		MATHFP_NODISCARD std::string to_string() const {
			if (entries_.empty()) return "{}";

			fmt::memory_buffer buf;
			fmt::format_to(std::back_inserter(buf), "{{");

			bool first = true;
			for (const auto& e : entries_) {
				if (!first) fmt::format_to(std::back_inserter(buf), ", ");
				first = false;
				fmt::format_to(std::back_inserter(buf), "{}={},", e.key, e.value);
				buf.resize(buf.size() - 1);
			}

			fmt::format_to(std::back_inserter(buf), "}}");
			return fmt::to_string(buf);
		}

	private:
		std::vector<Entry> entries_;
	};

} // namespace mathfp
