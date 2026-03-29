#include "detail/ui_internal.hpp"

#include <algorithm>
#include <utility>

#include <ftxui/screen/terminal.hpp>

namespace timetable::ui::detail {
    namespace {

        constexpr int kMinWrapWidth = 20;

    }  // namespace

    ftxui::Component make_renderer(
          const UiModel& model
        , UiState&       state
    ) {
        return ftxui::Renderer([&] {
            const auto snapshot     = model.snapshot();
            const auto size         = ftxui::Terminal::Size();
            const auto width        = std::max(kMinWrapWidth, size.dimx - 6);
            const auto total_height = std::max(16, size.dimy - 4);
            const auto heights      = compute_panel_heights(total_height);

            auto status_panel = build_panel(
                  "Status"
                , snapshot.status_lines
                , width
                , heights.status
                , state.status_scroll
                , state.focus == FocusPanel::Status
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
                  "Result"
                , snapshot.result.lines
                , width
                , heights.result
                , state.result_scroll
                , state.focus == FocusPanel::Result
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
                  "Logs"
                , snapshot.log_lines
                , width
                , heights.logs
                , state.logs_scroll
                , state.focus == FocusPanel::Logs
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

}  // namespace timetable::ui::detail
