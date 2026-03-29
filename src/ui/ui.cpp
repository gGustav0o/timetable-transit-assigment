#include "timetable/ui/ui.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <spdlog/spdlog.h>

#include <mathfp/core/expected.hpp>

#include "detail/ui_internal.hpp"

namespace timetable::ui {
    namespace {

        constexpr auto kUiRefreshInterval = std::chrono::milliseconds(100);

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

            ScopedRefreshLoop(const ScopedRefreshLoop&)            = delete;
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

    }  // namespace

    mathfp::Expected<mathfp::Unit> run(
          const UiModel&                         model
        , const std::shared_ptr<spdlog::logger>& logger
    ) {
        if (logger) {
            logger->info("UI started");
        }

        detail::UiState state;
        auto renderer = detail::make_renderer(model, state);

        auto screen = ftxui::ScreenInteractive::TerminalOutput();
        renderer    = detail::with_ui_event_handlers(renderer, screen, state, model, logger);
        ScopedRefreshLoop refresh_loop(screen);

        screen.PostEvent(ftxui::Event::Custom);
        screen.Loop(renderer);
        return mathfp::ok();
    }

}  // namespace timetable::ui
