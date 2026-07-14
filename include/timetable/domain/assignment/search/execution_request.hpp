#pragma once

#include <functional>
#include <optional>
#include <span>

#include "timetable/domain/model.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Search execution request passed from pipeline orchestration.
     *
     * This is execution metadata, not branch-and-bound kernel input.
     */
    struct SearchExecutionRequest final {
        SearchExecutionConfig config{};
        std::optional<std::reference_wrapper<const SearchTimeDomainExecution>>
            time_domain_execution{};
        std::span<const Zone> declared_zones{};
    };

}  // namespace timetable::domain::assignment
