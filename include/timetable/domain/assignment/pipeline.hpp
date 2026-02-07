#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/fp.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/steps.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Run the full timetable assignment pipeline.
     *
     * Order: preprocessing -> connection search -> connection choice -> demand split.
     */
    inline mathfp::Expected<DemandSplitResult> run_timetable_assignment_pipeline(
        AssignmentInput input
    ) {
        using mathfp::fp::pipe::and_then;
        timetable::infra::progress::both(
            "assignment pipeline started"
            , timetable::infra::LogLevel::Info
        );
        auto preprocessed = [&]() -> mathfp::Expected<PreprocessedNetwork> {
            if (input.presegmented) {
                return build_preprocessed_network_from_segments(
                    std::move(input.presegmented->route_segments)
                    , std::move(input.presegmented->connection_segments)
                );
            }
            return build_preprocessed_network(input.input, input.params.preprocess);
        };

        return
            preprocessed()
            | and_then([&](PreprocessedNetwork net) {
                net.fare_scale = compute_fare_scale(
                    net.connection_segments
                    , input.params.impedance.fare_normalization
                );
                return search_connections_branch_and_bound(net, input.params);
            })
            | and_then([&](ConnectionSearchResult search_result) {
                return choose_connections(search_result, input.params);
            })
            | and_then([&](ConnectionChoiceResult choice_result) {
                return split_demand_over_connections(choice_result, input.input, input.params);
            });
    }

}  // namespace timetable::domain::assignment
