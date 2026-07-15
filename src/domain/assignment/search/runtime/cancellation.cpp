#include "timetable/domain/assignment/search/runtime/cancellation.hpp"

namespace timetable::domain::assignment::runtime {

    bool search_cancelled(
        const SearchCancellationToken* token
    ) noexcept {
        return token != nullptr
            && token->requested.load(std::memory_order_acquire);
    }

    void request_search_cancellation(
        SearchCancellationToken* token
    ) noexcept {
        if (token != nullptr) {
            token->requested.store(true, std::memory_order_release);
        }
    }

}  // namespace timetable::domain::assignment::runtime
