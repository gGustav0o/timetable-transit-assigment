#pragma once

#include <atomic>

namespace timetable::domain::assignment::runtime {

    struct SearchCancellationToken final {
        std::atomic_bool requested{ false };
    };

    [[nodiscard]] bool search_cancelled(
        const SearchCancellationToken* token
    ) noexcept;

    void request_search_cancellation(
        SearchCancellationToken* token
    ) noexcept;

}  // namespace timetable::domain::assignment::runtime
