#include "detail/ui_internal.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

namespace timetable::ui::detail {
	namespace {

		constexpr int kMinWrapWidth = 20;
		constexpr int kMinStatusHeight = 3;
		constexpr int kPanelBorderHeight = 2;
		constexpr int kMinResultHeight = 6;
		constexpr int kMaxResultHeight = 16;
		constexpr int kMinLogsHeight = 4;

		struct PanelViewport final {
			std::vector<ftxui::Element> visible_lines{};
			int content_height{};
			ScrollTracking scroll_tracking{};
		};

		struct WrappedPanelContent final {
			std::vector<ftxui::Element> lines{};
		};

		void flush_wrapped_line(
			std::vector<std::string>& lines
			, std::string& current
		) {
			lines.push_back(std::move(current));
			current.clear();
		}

		std::size_t skip_spaces(
			std::string_view text
			, std::size_t index
		) {
			while (index < text.size() && text[index] == ' ') {
				++index;
			}
			return index;
		}

		std::size_t scan_token_end(
			std::string_view text
			, std::size_t index
		) {
			while (index < text.size() && text[index] != '\n' && text[index] != ' ') {
				++index;
			}
			return index;
		}

		void append_wrapped_token(
			std::vector<std::string>& lines
			, std::string& current
			, std::string_view token
			, int width
		) {
			if (!current.empty() && static_cast<int>(current.size() + 1 + token.size()) > width) {
				flush_wrapped_line(lines, current);
			}

			if (static_cast<int>(token.size()) > width) {
				std::size_t offset = 0;
				while (offset < token.size()) {
					const auto chunk = std::min<std::size_t>(
						static_cast<std::size_t>(width)
						, token.size() - offset
					);
					if (!current.empty()) {
						flush_wrapped_line(lines, current);
					}
					lines.emplace_back(token.substr(offset, chunk));
					offset += chunk;
				}
				return;
			}

			if (!current.empty()) {
				current.push_back(' ');
			}
			current.append(token);
		}

		std::vector<std::string> wrap_text(
			std::string_view text
			, int width
		) {
			std::vector<std::string> lines;
			if (width <= 0) {
				lines.emplace_back(text);
				return lines;
			}

			std::string current;
			current.reserve(static_cast<std::size_t>(width));

			std::size_t i = 0;
			while (i < text.size()) {
				if (text[i] == '\n') {
					flush_wrapped_line(lines, current);
					++i;
					continue;
				}

				const auto token_end = scan_token_end(text, i);
				append_wrapped_token(lines, current, text.substr(i, token_end - i), width);
				i = skip_spaces(text, token_end);
			}

			flush_wrapped_line(lines, current);
			return lines;
		}

		ftxui::Color color_for_level(
			infra::LogLevel level
		) {
			switch (level) {
				case infra::LogLevel::Error:
					return ftxui::Color::Red;
				case infra::LogLevel::Warning:
					return ftxui::Color::Yellow;
				case infra::LogLevel::Debug:
					return ftxui::Color::GrayLight;
				case infra::LogLevel::Info:
					return ftxui::Color::Blue;
			}
			return ftxui::Color::Default;
		}

		std::vector<ftxui::Element> wrap_log_elements(
			const std::vector<infra::LogEntry>& lines
			, int wrap_width
		) {
			std::vector<ftxui::Element> out;
			out.reserve(lines.size());
			for (const auto& line : lines) {
				const auto color = color_for_level(line.level);
				auto wrapped = wrap_text(line.message, wrap_width);
				for (auto& chunk : wrapped) {
					out.push_back(ftxui::text(std::move(chunk)) | ftxui::color(color));
				}
			}
			if (out.empty()) {
				out.push_back(ftxui::text("-"));
			}
			return out;
		}

		std::vector<ftxui::Element> wrap_string_elements(
			const std::vector<std::string>& lines
			, int wrap_width
		) {
			std::vector<ftxui::Element> out;
			for (const auto& line : lines) {
				auto wrapped = wrap_text(line, wrap_width);
				for (auto& chunk : wrapped) {
					out.push_back(ftxui::text(std::move(chunk)));
				}
			}
			if (out.empty()) {
				out.push_back(ftxui::text("-"));
			}
			return out;
		}

		std::vector<ftxui::Element> slice_elements(
			const std::vector<ftxui::Element>& lines
			, int offset
			, int height
		) {
			std::vector<ftxui::Element> out;
			if (lines.empty() || height <= 0) {
				return out;
			}
			const auto start = static_cast<std::size_t>(std::max(0, offset));
			const auto end = static_cast<std::size_t>(
				std::min<int>(static_cast<int>(lines.size()), offset + height)
			);
			out.reserve(end - start);
			for (std::size_t i = start; i < end; ++i) {
				out.push_back(lines[i]);
			}
			return out;
		}

		int max_scroll(
			int total
			, int height
		) {
			const auto max_offset = std::max(0, total - std::max(1, height));
			return max_offset;
		}

		int clamp_scroll(
			int offset
			, int total
			, int height
		) {
			return std::clamp(offset, 0, max_scroll(total, height));
		}

		ScrollTracking auto_scroll(
			int offset
			, int total
			, int height
			, int last_total
		) {
			if (total > last_total) {
				return ScrollTracking{
					.offset = max_scroll(total, height)
					, .last_total = total
				};
			}
			return ScrollTracking{
				.offset = offset
				, .last_total = total
			};
		}

		WrappedPanelContent prepare_wrapped_panel_content(
			const std::vector<infra::LogEntry>& lines
			, int wrap_width
		) {
			return WrappedPanelContent{
				.lines = wrap_log_elements(lines, wrap_width)
			};
		}

		WrappedPanelContent prepare_wrapped_panel_content(
			const std::vector<std::string>& lines
			, int wrap_width
		) {
			return WrappedPanelContent{
				.lines = wrap_string_elements(lines, wrap_width)
			};
		}

		PanelViewport prepare_panel_viewport(
			WrappedPanelContent content
			, int panel_height
			, ScrollTracking scroll_tracking
		) {
			const auto content_height = std::max(1, panel_height - kPanelBorderHeight);
			auto tracked = auto_scroll(
				scroll_tracking.offset
				, static_cast<int>(content.lines.size())
				, content_height
				, scroll_tracking.last_total
			);
			const auto clamped_offset = clamp_scroll(
				tracked.offset
				, static_cast<int>(content.lines.size())
				, content_height
			);

			return PanelViewport{
				.visible_lines = slice_elements(content.lines, clamped_offset, content_height)
				, .content_height = content_height
				, .scroll_tracking = ScrollTracking{
					.offset = clamped_offset
					, .last_total = tracked.last_total
				}
			};
		}

		ftxui::Element make_panel_title(
			const char* title
			, bool focused
		) {
			const auto label = focused
				? std::string("> ") + title + " [c copy]"
				: std::string(title);
			return focused
				? ftxui::text(label) | ftxui::bold
				: ftxui::text(label);
		}

		ftxui::Element render_panel_window(
			ftxui::Element title
			, std::vector<ftxui::Element> visible_lines
		) {
			return ftxui::window(
				std::move(title)
				, ftxui::vbox(std::move(visible_lines)) | ftxui::flex
			);
		}

		template <class Lines>
		BuiltPanel build_panel_impl(
			const char* title
			, const Lines& lines
			, int wrap_width
			, int panel_height
			, int scroll
			, bool focused
			, int last_total
		) {
			auto content = prepare_wrapped_panel_content(lines, wrap_width);
			auto viewport = prepare_panel_viewport(
				std::move(content)
				, panel_height
				, ScrollTracking{
					.offset = scroll
					, .last_total = last_total
				}
			);
			return BuiltPanel{
				.element = render_panel_window(
					make_panel_title(title, focused)
					, std::move(viewport.visible_lines)
				)
				, .scroll_tracking = viewport.scroll_tracking
			};
		}

	}  // namespace

	PanelHeights compute_panel_heights(
		int total_height
	) {
		PanelHeights heights{
			.status = std::clamp(total_height / 6, kMinStatusHeight, 6)
			, .result = std::clamp(total_height / 3, kMinResultHeight, kMaxResultHeight)
			, .logs = 0
		};

		heights.logs = total_height - heights.status - heights.result - 2;
		if (heights.logs < kMinLogsHeight) {
			heights.result = std::max(
				kMinResultHeight
				, heights.result - (kMinLogsHeight - heights.logs)
			);
			heights.logs = total_height - heights.status - heights.result - 2;
		}

		if (heights.logs < kMinLogsHeight) {
			heights.status = std::max(
				kMinStatusHeight
				, heights.status - (kMinLogsHeight - heights.logs)
			);
			heights.logs = total_height - heights.status - heights.result - 2;
		}

		heights.logs = std::max(kMinLogsHeight, heights.logs);
		return heights;
	}

	BuiltPanel build_panel(
		const char* title
		, const std::vector<infra::LogEntry>& lines
		, int wrap_width
		, int panel_height
		, int scroll
		, bool focused
		, int last_total
	) {
		return build_panel_impl(
			title
			, lines
			, wrap_width
			, panel_height
			, scroll
			, focused
			, last_total
		);
	}

	BuiltPanel build_panel(
		const char* title
		, const std::vector<std::string>& lines
		, int wrap_width
		, int panel_height
		, int scroll
		, bool focused
		, int last_total
	) {
		return build_panel_impl(
			title
			, lines
			, wrap_width
			, panel_height
			, scroll
			, focused
			, last_total
		);
	}

	void apply_scroll_tracking(
		int& scroll
		, int& last_total
		, const BuiltPanel& panel
	) {
		scroll = panel.scroll_tracking.offset;
		last_total = panel.scroll_tracking.last_total;
	}

}  // namespace timetable::ui::detail
