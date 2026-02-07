#include "timetable/ui/ui.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <mathfp/core/expected.hpp>

namespace timetable::ui {
	namespace {

		constexpr auto kUiRefreshInterval = std::chrono::milliseconds(100);
		constexpr int kMinWrapWidth = 20;
		constexpr int kMinStatusHeight = 3;
		constexpr int kMaxStatusHeight = 8;
		constexpr int kPanelBorderHeight = 2;

		enum class FocusPanel {
			Status,
			Logs
		};

		struct UiState final {
			int status_scroll = 0;
			int logs_scroll = 0;
			FocusPanel focus = FocusPanel::Logs;
			int last_status_lines = 0;
			int last_log_lines = 0;
		};

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
					lines.push_back(std::move(current));
					current.clear();
					++i;
					continue;
				}

				const auto start = i;
				while (i < text.size() && text[i] != '\n' && text[i] != ' ') {
					++i;
				}
				const auto token = text.substr(start, i - start);

				if (!current.empty() && static_cast<int>(current.size() + 1 + token.size()) > width) {
					lines.push_back(std::move(current));
					current.clear();
				}

				if (static_cast<int>(token.size()) > width) {
					std::size_t offset = 0;
					while (offset < token.size()) {
						const auto chunk = std::min<std::size_t>(
							static_cast<std::size_t>(width),
							token.size() - offset
						);
						if (!current.empty()) {
							lines.push_back(std::move(current));
							current.clear();
						}
						lines.emplace_back(token.substr(offset, chunk));
						offset += chunk;
					}
				} else {
					if (!current.empty()) {
						current.push_back(' ');
					}
					current.append(token);
				}

				while (i < text.size() && text[i] == ' ') {
					++i;
				}
			}

			lines.push_back(std::move(current));
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

		void clamp_scroll(int& offset, int total, int height) {
			offset = std::clamp(offset, 0, max_scroll(total, height));
		}

		void auto_scroll(
			int& offset
			, int total
			, int height
			, int& last_total
		) {
			if (total > last_total) {
				offset = max_scroll(total, height);
				last_total = total;
				return;
			}
			last_total = total;
		}

		ftxui::Element build_panel(
			const char* title
			, const std::vector<infra::LogEntry>& lines
			, int wrap_width
			, int panel_height
			, int& scroll
			, bool focused
			, int& last_total
		) {
			const auto content_height = std::max(1, panel_height - kPanelBorderHeight);
			auto wrapped = wrap_elements(lines, wrap_width);
			auto_scroll(scroll, static_cast<int>(wrapped.size()), content_height, last_total);
			clamp_scroll(scroll, static_cast<int>(wrapped.size()), content_height);
			auto visible = slice_elements(wrapped, scroll, content_height);
			auto content = ftxui::vbox(std::move(visible));
			auto title_text = focused
				? ftxui::text(std::string("> ") + title) | ftxui::bold
				: ftxui::text(title);
			return ftxui::window(
				title_text
				, content | ftxui::flex
			);
		}

		ftxui::Component make_renderer(const UiModel& model, UiState& state) {
			return ftxui::Renderer([&] {
				const auto snapshot = model.snapshot();
				const auto size = ftxui::Terminal::Size();
				const auto width = std::max(kMinWrapWidth, size.dimx - 6);
				const auto total_height = std::max(10, size.dimy - 4);
				const auto status_height = std::clamp(
					total_height / 4,
					kMinStatusHeight,
					kMaxStatusHeight
				);
				const auto logs_height = std::max(3, total_height - status_height - 1);

				auto status_box = build_panel(
					"Status", snapshot.status_lines, width, status_height,
					state.status_scroll, state.focus == FocusPanel::Status,
					state.last_status_lines
				) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, status_height);

				auto logs_box = build_panel(
					"Logs", snapshot.log_lines, width, logs_height,
					state.logs_scroll, state.focus == FocusPanel::Logs,
					state.last_log_lines
				) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, logs_height);

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
			, UiState& state
		) {
			return CatchEvent(renderer, [&](const ftxui::Event& event) {
				if (event == ftxui::Event::Character('q') ||
					event == ftxui::Event::Escape || event == ftxui::Event::CtrlC) {
					screen.Exit();
					return true;
				}
				if (event == ftxui::Event::Tab) {
					state.focus = (state.focus == FocusPanel::Logs)
						? FocusPanel::Status
						: FocusPanel::Logs;
					return true;
				}
				const auto delta = [&](int d) {
					if (state.focus == FocusPanel::Logs)
						state.logs_scroll += d;
					else
						state.status_scroll += d;
				};
				if (event == ftxui::Event::ArrowUp) { delta(-1); return true; }
				if (event == ftxui::Event::ArrowDown) { delta(1); return true; }
				if (event == ftxui::Event::PageUp) { delta(-10); return true; }
				if (event == ftxui::Event::PageDown) { delta(10); return true; }
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

		UiState state;
		auto renderer = make_renderer(model, state);

		auto screen = ftxui::ScreenInteractive::TerminalOutput();
		renderer = with_exit_handler(renderer, screen, state);

		std::atomic_bool running = true;
		auto refresh_thread = std::thread([&] {
			while (running.load(std::memory_order_relaxed)) {
				std::this_thread::sleep_for(kUiRefreshInterval);
				screen.PostEvent(ftxui::Event::Custom);
			}
		});

		screen.PostEvent(ftxui::Event::Custom);
		screen.Loop(renderer);
		running.store(false, std::memory_order_relaxed);
		if (refresh_thread.joinable()) {
			refresh_thread.join();
		}
		return mathfp::ok();
	}

}  // namespace timetable::ui
