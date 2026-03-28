#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/fp.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/split.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace detail {

        inline mathfp::Expected<PreprocessedNetwork> build_preprocessed_step(
            AssignmentInput& input
        ) {
            // Explicitly consume pre-segmented payload to avoid copying large vectors.
            if (input.presegmented) {
                return build_preprocessed_network_from_segments(
                    std::move(input.presegmented->route_segments)
                    , std::move(input.presegmented->connection_segments)
                );
            }
            return build_preprocessed_network(input.input, input.params.preprocess);
        }

        inline mathfp::Expected<ConnectionSearchResult> run_search_step(
            PreprocessedNetwork net
            , const SearchParams& params
        ) {
            const auto fare_scale = compute_fare_scale(
                net.connection_segments
                , params.impedance.fare_normalization
            );
            return search_connections_branch_and_bound(net, fare_scale, params);
        }

    }  // namespace detail

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
        return
            detail::build_preprocessed_step(input)
            | and_then([&](PreprocessedNetwork net) {
                return detail::run_search_step(std::move(net), input.params);
            })
            | and_then([&](ConnectionSearchResult search_result) {
                return choose_connections(search_result, input.params);
            })
            | and_then([&](ConnectionChoiceResult choice_result) {
                return split_demand_over_connections(choice_result, input.input, input.params);
            });
    }

}  // namespace timetable::domain::assignment
