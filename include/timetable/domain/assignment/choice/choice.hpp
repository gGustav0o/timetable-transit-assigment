#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/search/search.hpp"

namespace timetable::domain::assignment {

    struct ConnectionChoiceResult final {
        std::vector<DiscoveredConnection> connections{};
    };

    /**
     * @brief Apply choice criteria to remove dominated/illogical connections.
     */
    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const ChoiceConfig&           config
    );

}  // namespace timetable::domain::assignment
