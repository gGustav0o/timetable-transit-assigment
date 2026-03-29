#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "timetable/infra/log_entry.hpp"
#include "timetable/ui/model.hpp"

namespace spdlog {
	class logger;
}  // namespace spdlog

namespace timetable::ui::detail {

	enum class FocusPanel {
		Status
		, Result
		, Logs
	};

	struct UiState final {
		int status_scroll = 0;
		int result_scroll = 0;
		int logs_scroll = 0;
		int last_status_lines = 0;
		int last_result_lines = 0;
		int last_log_lines = 0;
		FocusPanel focus = FocusPanel::Logs;
	};

	struct ScrollTracking final {
		int offset = 0;
		int last_total = 0;
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

	PanelHeights compute_panel_heights(
		int total_height
	);

	BuiltPanel build_panel(
		const char* title
		, const std::vector<infra::LogEntry>& lines
		, int wrap_width
		, int panel_height
		, int scroll
		, bool focused
		, int last_total
	);

	BuiltPanel build_panel(
		const char* title
		, const std::vector<std::string>& lines
		, int wrap_width
		, int panel_height
		, int scroll
		, bool focused
		, int last_total
	);

	void apply_scroll_tracking(
		int& scroll
		, int& last_total
		, const BuiltPanel& panel
	);

	ftxui::Component make_renderer(
		const UiModel& model
		, UiState& state
	);

	ftxui::Component with_ui_event_handlers(
		ftxui::Component renderer
		, ftxui::ScreenInteractive& screen
		, UiState& state
		, const UiModel& model
		, const std::shared_ptr<spdlog::logger>& logger
	);

}  // namespace timetable::ui::detail
