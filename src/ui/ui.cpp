#include "timetable/ui/ui.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <mathfp/core/expected.hpp>

#include "timetable/infra/clipboard.hpp"

namespace timetable::ui {
	namespace {

		constexpr auto kUiRefreshInterval = std::chrono::milliseconds(100);
		constexpr int kMinWrapWidth      = 20;
		constexpr int kMinStatusHeight   = 3;
		constexpr int kPanelBorderHeight = 2;
		constexpr int kMinResultHeight   = 6;
		constexpr int kMaxResultHeight   = 16;
		constexpr int kMinLogsHeight     = 4;

		enum class FocusPanel {
			Status
			, Result
			, Logs
		};

		struct UiState final {
			int status_scroll     = 0;
			int result_scroll     = 0;
			int logs_scroll       = 0;
			int last_status_lines = 0;
			int last_result_lines = 0;
			int last_log_lines    = 0;
			FocusPanel focus = FocusPanel::Logs;
		};

		struct ScrollTracking final {
			int offset     = 0;
			int last_total = 0;
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

		std::vector<std::string> wrap_text(std::string_view text, int width) {
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

		std::vector<ftxui::Element> wrap_elements(
			const std::vector<infra::LogEntry>& lines
			, int wrap_width
		);

		ftxui::Color color_for_level(infra::LogLevel level) {
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

		std::vector<ftxui::Element> wrap_elements(
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

		int max_scroll(int total, int height) {
			const auto max_offset = std::max(0, total - std::max(1, height));
			return max_offset;
		}

		int clamp_scroll(int offset, int total, int height) {
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

		struct PanelViewport final {
			std::vector<ftxui::Element> visible_lines{};
			int content_height{};
			ScrollTracking scroll_tracking{};
		};

		struct BuiltPanel final {
			ftxui::Element element;
			ScrollTracking scroll_tracking{};
		};

		struct PanelHeights final {
			int status{};
			int result{};
			int logs{};
		};

		std::string_view panel_name(FocusPanel panel) noexcept {
			switch (panel) {
				case FocusPanel::Status:
					return "status";
				case FocusPanel::Result:
					return "result";
				case FocusPanel::Logs:
					return "logs";
			}
			return "logs";
		}

		std::string_view log_level_label(infra::LogLevel level) noexcept {
			switch (level) {
				case infra::LogLevel::Info:
					return "INFO";
				case infra::LogLevel::Warning:
					return "WARN";
				case infra::LogLevel::Error:
					return "ERROR";
				case infra::LogLevel::Debug:
					return "DEBUG";
			}
			return "INFO";
		}

		std::span<const infra::LogEntry> focused_panel_lines(
			const UiSnapshot& snapshot
			, FocusPanel panel
		) noexcept {
			switch (panel) {
				case FocusPanel::Status:
					return snapshot.status_lines;
				case FocusPanel::Logs:
					return snapshot.log_lines;
				case FocusPanel::Result:
					break;
			}
			return {};
		}

		std::span<const std::string> focused_result_lines(
			const UiSnapshot& snapshot
			, FocusPanel panel
		) noexcept {
			if (panel == FocusPanel::Result) {
				return snapshot.result.lines;
			}
			return {};
		}

		std::string serialize_log_entries_for_clipboard(
			std::span<const infra::LogEntry> lines
		) {
			std::string out;
			for (std::size_t i = 0; i < lines.size(); ++i) {
				const auto& entry = lines[i];
				out.append("[");
				out.append(log_level_label(entry.level));
				out.append("] ");
				out.append(entry.message);
				if (i + 1 < lines.size()) {
					out.append("\n");
				}
			}
			return out;
		}

		mathfp::Expected<mathfp::Unit> copy_focused_panel_to_clipboard(
			const UiSnapshot& snapshot
			, FocusPanel panel
		) {
			if (panel == FocusPanel::Result) {
				std::string text;
				const auto lines = focused_result_lines(snapshot, panel);
				for (std::size_t i = 0; i < lines.size(); ++i) {
					text.append(lines[i]);
					if (i + 1 < lines.size()) {
						text.push_back('\n');
					}
				}
				return infra::copy_text_to_clipboard(text);
			}

			return infra::copy_text_to_clipboard(
				serialize_log_entries_for_clipboard(focused_panel_lines(snapshot, panel))
			);
		}

		struct WrappedPanelContent final {
			std::vector<ftxui::Element> lines{};
		};

		WrappedPanelContent prepare_wrapped_panel_content(
			const std::vector<infra::LogEntry>& lines
			, int wrap_width
		) {
			return WrappedPanelContent{
				.lines = wrap_elements(lines, wrap_width)
			};
		}

		WrappedPanelContent prepare_wrapped_panel_content(
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
			return WrappedPanelContent{
				.lines = std::move(out)
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

		bool is_exit_event(const ftxui::Event& event) {
			return event == ftxui::Event::Character('q')
				|| event == ftxui::Event::Escape
				|| event == ftxui::Event::CtrlC;
		}

		bool is_focus_switch_event(const ftxui::Event& event) {
			return event == ftxui::Event::Tab;
		}

		bool is_copy_event(const ftxui::Event& event) {
			return event == ftxui::Event::Character('c')
				|| event == ftxui::Event::Character('C');
		}

		UiState toggle_focus(UiState state) {
			switch (state.focus) {
				case FocusPanel::Status:
					state.focus = FocusPanel::Result;
					break;
				case FocusPanel::Result:
					state.focus = FocusPanel::Logs;
					break;
				case FocusPanel::Logs:
					state.focus = FocusPanel::Status;
					break;
			}
			return state;
		}

		int scroll_delta(const ftxui::Event& event) {
			if (event == ftxui::Event::ArrowUp) {
				return -1;
			}
			if (event == ftxui::Event::ArrowDown) {
				return 1;
			}
			if (event == ftxui::Event::PageUp) {
				return -10;
			}
			if (event == ftxui::Event::PageDown) {
				return 10;
			}
			return 0;
		}

		UiState apply_scroll_delta(
			UiState state
			, int delta
		) {
			if (delta == 0) {
				return state;
			}

			if (state.focus == FocusPanel::Logs) {
				state.logs_scroll += delta;
				return state;
			}

			if (state.focus == FocusPanel::Result) {
				state.result_scroll += delta;
				return state;
			}

			state.status_scroll += delta;
			return state;
		}

		bool handle_exit_event(
			const ftxui::Event& event
			, ftxui::ScreenInteractive& screen
		) {
			if (!is_exit_event(event)) {
				return false;
			}

			screen.Exit();
			return true;
		}

		bool handle_focus_event(
			const ftxui::Event& event
			, UiState& state
		) {
			if (!is_focus_switch_event(event)) {
				return false;
			}

			state = toggle_focus(state);
			return true;
		}

		bool handle_copy_event(
			const ftxui::Event& event
			, const UiModel& model
			, const UiState& state
			, const std::shared_ptr<spdlog::logger>& logger
		) {
			if (!is_copy_event(event)) {
				return false;
			}

			const auto snapshot = model.snapshot();
			auto copy_result = copy_focused_panel_to_clipboard(snapshot, state.focus);
			if (logger) {
				if (copy_result) {
					logger->info("copied {} panel to clipboard", panel_name(state.focus));
				} else {
					logger->error(
						"failed to copy {} panel to clipboard:\n{}"
						, panel_name(state.focus)
						, copy_result.error().to_string()
					);
				}
			}
			return true;
		}

		bool handle_scroll_event(
			const ftxui::Event& event
			, UiState& state
		) {
			const auto delta = scroll_delta(event);
			if (delta == 0) {
				return false;
			}

			state = apply_scroll_delta(state, delta);
			return true;
		}

		class ScopedRefreshLoop final {
		public:
			explicit ScopedRefreshLoop(ftxui::ScreenInteractive& screen)
				: screen_(screen)
				, refresh_thread_([this] {
					while (running_.load(std::memory_order_relaxed)) {
						std::this_thread::sleep_for(kUiRefreshInterval);
						screen_.PostEvent(ftxui::Event::Custom);
					}
				}) {
			}

			ScopedRefreshLoop(const ScopedRefreshLoop&) = delete;
			ScopedRefreshLoop& operator=(const ScopedRefreshLoop&) = delete;

			~ScopedRefreshLoop() {
				running_.store(false, std::memory_order_relaxed);
				if (refresh_thread_.joinable()) {
					refresh_thread_.join();
				}
			}

		private:
			ftxui::ScreenInteractive& screen_;
			std::atomic_bool running_{ true };
			std::thread refresh_thread_;
		};

		BuiltPanel build_panel(
			const char* title
			, const std::vector<infra::LogEntry>& lines
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

		BuiltPanel build_panel(
			const char* title
			, const std::vector<std::string>& lines
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

		void apply_scroll_tracking(
			int& scroll
			, int& last_total
			, const BuiltPanel& panel
		) {
			scroll = panel.scroll_tracking.offset;
			last_total = panel.scroll_tracking.last_total;
		}

		ftxui::Component make_renderer(const UiModel& model, UiState& state) {
			return ftxui::Renderer([&] {
				const auto snapshot     = model.snapshot();
				const auto size         = ftxui::Terminal::Size();
				const auto width        = std::max(kMinWrapWidth, size.dimx - 6);
				const auto total_height = std::max(16, size.dimy - 4);
				const auto heights      = compute_panel_heights(total_height);

				auto status_panel = build_panel(
					"Status", snapshot.status_lines, width, heights.status
					, state.status_scroll, state.focus == FocusPanel::Status
					, state.last_status_lines
				);
				auto status_box = std::move(status_panel.element)
					| ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, heights.status);
				apply_scroll_tracking(
					state.status_scroll
					, state.last_status_lines
					, status_panel
				);

				auto result_panel = build_panel(
					"Result", snapshot.result.lines, width, heights.result
					, state.result_scroll, state.focus == FocusPanel::Result
					, state.last_result_lines
				);
				auto result_box = std::move(result_panel.element)
					| ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, heights.result);
				apply_scroll_tracking(
					state.result_scroll
					, state.last_result_lines
					, result_panel
				);

				auto logs_panel = build_panel(
					"Logs", snapshot.log_lines, width, heights.logs
					, state.logs_scroll, state.focus == FocusPanel::Logs
					, state.last_log_lines
				);
				auto logs_box = std::move(logs_panel.element)
					| ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, heights.logs);
				apply_scroll_tracking(
					state.logs_scroll
					, state.last_log_lines
					, logs_panel
				);

				return ftxui::vbox({
						   status_box
						   , ftxui::separator()
						   , result_box
						   , ftxui::separator()
						   , logs_box
					})
					| ftxui::border;
			});
		}

		ftxui::Component with_exit_handler(
			ftxui::Component renderer
			, ftxui::ScreenInteractive& screen
			, UiState& state
			, const UiModel& model
			, const std::shared_ptr<spdlog::logger>& logger
		) {
			return CatchEvent(renderer, [&](const ftxui::Event& event) {
				return handle_exit_event(event, screen)
					|| handle_focus_event(event, state)
					|| handle_copy_event(event, model, state, logger)
					|| handle_scroll_event(event, state);
			});
		}

	}  // namespace

	mathfp::Expected<mathfp::Unit> run(
		const UiModel& model
		, const std::shared_ptr<spdlog::logger>& logger
	) {
		if (logger) {
			logger->info("UI started");
		}

		UiState state;
		auto renderer = make_renderer(model, state);

		auto screen = ftxui::ScreenInteractive::TerminalOutput();
		renderer = with_exit_handler(renderer, screen, state, model, logger);
		ScopedRefreshLoop refresh_loop(screen);

		screen.PostEvent(ftxui::Event::Custom);
		screen.Loop(renderer);
		return mathfp::ok();
	}

}  // namespace timetable::ui
