#include "timetable/ui/ui.hpp"

#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <mathfp/core/expected.hpp>

namespace timetable::ui {
	namespace {

		std::vector<ftxui::Element> to_elements(const std::vector<std::string>& lines) {
			std::vector<ftxui::Element> out;
			out.reserve(lines.size());
			for (const auto& line : lines) {
				out.push_back(ftxui::text(line));
			}
			if (out.empty()) {
				out.push_back(ftxui::text("-"));
			}
			return out;
		}

	}  // namespace

	mathfp::Expected<mathfp::Unit> run(
		const UiModel& model
		, const std::shared_ptr<spdlog::logger>& logger
	) {
		if (logger) {
			logger->info("UI started");
		}

		auto renderer = ftxui::Renderer([&] {
			const auto snapshot = model.snapshot();
			auto status_box = ftxui::window(
				ftxui::text("Status"), ftxui::vbox(to_elements(snapshot.status_lines)));
			auto logs_box = ftxui::window(
				ftxui::text("Logs"), ftxui::vbox(to_elements(snapshot.log_lines)));

			return ftxui::vbox({
					   status_box | ftxui::flex,
					   ftxui::separator(),
					   logs_box | ftxui::flex,
				}) |
				ftxui::border;
			});

		auto screen = ftxui::ScreenInteractive::TerminalOutput();
		renderer = CatchEvent(renderer, [&](const ftxui::Event& event) {
			if (event == ftxui::Event::Character('q') ||
				event == ftxui::Event::Escape || event == ftxui::Event::CtrlC) {
				screen.Exit();
				return true;
			}
			return false;
			});

		screen.Loop(renderer);
		return mathfp::ok();
	}

}  // namespace timetable::ui
