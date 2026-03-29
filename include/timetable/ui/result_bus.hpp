#pragma once

#include <functional>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/ui/result_snapshot.hpp"

namespace timetable::ui::result {

    using Sink = std::function<void(UiResultSnapshot)>;

    mathfp::Expected<mathfp::Unit> set_sink(Sink sink);
    mathfp::Expected<mathfp::Unit> clear_sink();

    void publish(UiResultSnapshot snapshot);

}  // namespace timetable::ui::result
