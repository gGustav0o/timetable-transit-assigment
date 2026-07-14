#pragma once

#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/id.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/assignment/search/connection.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"

namespace timetable::domain::assignment {

    struct SearchTaskRefTag {};

    using SearchTaskRef = DomainId<SearchTaskRefTag>;

    /**
     * @brief OD-time demand task.
     *
     * This belongs to assignment projection, not to the branch-and-bound kernel.
     * Kernel search may share trees between tasks, but demand intervals remain
     * projection and split concerns.
     */
    struct SearchTask final {
        SearchTaskRef    index;
        ZoneId           origin;
        ZoneId           destination;
        TimeInterval     interval;
        SearchTimeDomain departure_domain{};
    };

    struct SearchTaskResult final {
        SearchTask                    task;
        std::vector<SearchConnection> connections{};
    };

    struct ConnectionSearchResult final {
        std::vector<SearchTaskResult> task_results{};
    };

    mathfp::Expected<std::vector<SearchTask>> build_search_tasks(
        const InputModel& input
    );

    [[nodiscard]] std::vector<const SearchConnection*> search_connection_ptrs(
        const ConnectionSearchResult& result
    );

    [[nodiscard]] std::size_t search_connection_count(
        const ConnectionSearchResult& result
    ) noexcept;

}  // namespace timetable::domain::assignment
