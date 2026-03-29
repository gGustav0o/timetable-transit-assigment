#pragma once

#include <cstddef>
#include <vector>
#include <string>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::ui {

    struct UiResultSnapshot final {
        std::vector<std::string> lines{};
    };

    struct UiResultSnapshotOptions final {
        std::size_t max_od_results{ 8 };
        std::size_t max_connections_per_od{ 4 };
        bool        include_empty_ods{ false };
    };

    UiResultSnapshot make_pending_result_snapshot();
    UiResultSnapshot make_unavailable_result_snapshot();

    mathfp::Expected<UiResultSnapshot> build_result_snapshot(
          const timetable::domain::AssignmentOutput& output
        , const UiResultSnapshotOptions&             options = {}
    );

}  // namespace timetable::ui
