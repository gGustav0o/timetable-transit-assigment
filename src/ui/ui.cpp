#include "timetable/ui/ui.hpp"

#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <mathfp/core/expected.hpp>

namespace timetable::ui {
	namespace {

		std::vector<ftxui::Element> to_elements(
			const std::vector<infra::LogEntry>& lines
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

		std::vector<ftxui::Element> to_elements(
			const std::vector<infra::LogEntry>& lines
		) {
			std::vector<ftxui::Element> out;
			out.reserve(lines.size());
			for (const auto& line : lines) {
				auto color = color_for_level(line.level);
				out.push_back(ftxui::paragraph(line.message) | ftxui::color(color));
			}
			if (out.empty()) {
				out.push_back(ftxui::text("-"));
			}
			return out;
		}

		ftxui::Element build_panel(
			const char* title
			, const std::vector<infra::LogEntry>& lines
		) {
			auto content = ftxui::vbox(to_elements(lines));
			return ftxui::window(
				ftxui::text(title)
				, content | ftxui::frame | ftxui::vscroll_indicator
			);
		}

		ftxui::Component make_renderer(const UiModel& model) {
			return ftxui::Renderer([&] {
				const auto snapshot = model.snapshot();
				auto status_box = build_panel("Status", snapshot.status_lines) | ftxui::flex;
				auto logs_box   = build_panel("Logs", snapshot.log_lines) | ftxui::flex;

				return ftxui::vbox({
						   status_box
						   , ftxui::separator()
						   , logs_box
					})
					| ftxui::border;
			});
		}

		ftxui::Component with_exit_handler(
			ftxui::Component renderer
			, ftxui::ScreenInteractive& screen
		) {
			return CatchEvent(renderer, [&](const ftxui::Event& event) {
				if (event == ftxui::Event::Character('q') ||
					event == ftxui::Event::Escape || event == ftxui::Event::CtrlC) {
					screen.Exit();
					return true;
				}
				return false;
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

		auto renderer = make_renderer(model);

		auto screen = ftxui::ScreenInteractive::TerminalOutput();
		renderer = with_exit_handler(renderer, screen);

		screen.PostEvent(ftxui::Event::Custom);
		screen.Loop(renderer);
		return mathfp::ok();
	}

}  // namespace timetable::ui
