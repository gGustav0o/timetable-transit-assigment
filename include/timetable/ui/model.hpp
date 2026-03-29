#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "timetable/infra/log_entry.hpp"
#include "timetable/ui/result_snapshot.hpp"

namespace timetable::ui {

    struct UiSnapshot {
        std::vector<infra::LogEntry> status_lines;
        UiResultSnapshot             result;
        std::vector<infra::LogEntry> log_lines;
    };

    class UiModel {
    public:
        UiSnapshot snapshot() const;
        void set_status_lines(std::vector<infra::LogEntry> lines);
        void set_result_snapshot(UiResultSnapshot snapshot);
        void set_log_lines(std::vector<infra::LogEntry> lines);

    private:
        mutable std::mutex mutex_;
        std::vector<infra::LogEntry> status_lines_;
        UiResultSnapshot result_;
        std::vector<infra::LogEntry> log_lines_;
    };

}  // namespace timetable::ui
