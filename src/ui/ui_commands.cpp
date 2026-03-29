#include "detail/ui_internal.hpp"

#include <span>
#include <string>
#include <string_view>

#include <ftxui/component/event.hpp>
#include <spdlog/spdlog.h>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/infra/clipboard.hpp"

namespace timetable::ui::detail {
	namespace {

		std::string_view panel_name(
			FocusPanel panel
		) noexcept {
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

		std::string_view log_level_label(
			infra::LogLevel level
		) noexcept {
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
					out.push_back('\n');
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

		bool is_exit_event(
			const ftxui::Event& event
		) {
			return event == ftxui::Event::Character('q')
				|| event == ftxui::Event::Escape
				|| event == ftxui::Event::CtrlC;
		}

		bool is_focus_switch_event(
			const ftxui::Event& event
		) {
			return event == ftxui::Event::Tab;
		}

		bool is_copy_event(
			const ftxui::Event& event
		) {
			return event == ftxui::Event::Character('c')
				|| event == ftxui::Event::Character('C');
		}

		UiState toggle_focus(
			UiState state
		) {
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

		int scroll_delta(
			const ftxui::Event& event
		) {
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

		bool handle_ui_event(
			const ftxui::Event& event
			, ftxui::ScreenInteractive& screen
			, UiState& state
			, const UiModel& model
			, const std::shared_ptr<spdlog::logger>& logger
		) {
			return handle_exit_event(event, screen)
				|| handle_focus_event(event, state)
				|| handle_copy_event(event, model, state, logger)
				|| handle_scroll_event(event, state);
		}

	}  // namespace

	ftxui::Component with_ui_event_handlers(
		ftxui::Component renderer
		, ftxui::ScreenInteractive& screen
		, UiState& state
		, const UiModel& model
		, const std::shared_ptr<spdlog::logger>& logger
	) {
		return ftxui::CatchEvent(renderer, [&](const ftxui::Event& event) {
			return handle_ui_event(event, screen, state, model, logger);
		});
	}

}  // namespace timetable::ui::detail
