#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/split.hpp"
#include "timetable/domain/assignment/validation.hpp"
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

        inline mathfp::Expected<PreprocessedNetwork> run_validated_preprocessing_step(
            AssignmentInput& input
        ) {
            MATHFP_TRY(validate_preprocessing_step_input(input));
            return build_preprocessed_step(input);
        }

        inline mathfp::Expected<ConnectionSearchResult> run_validated_search_step(
            PreprocessedNetwork net
            , const SearchParams& params
        ) {
            MATHFP_TRY(validate_preprocessing_step_output(net, params));
            const auto fare_scale = compute_fare_scale(
                net.connection_segments
                , params.impedance.fare_normalization
            );
            MATHFP_TRY_LET(
                ConnectionSearchResult
                , search_result
                , search_connections_branch_and_bound(net, fare_scale, params)
            );
            MATHFP_TRY(validate_search_step_output(
                search_result
                , net
                , fare_scale
                , params
            ));
            return mathfp::Expected<ConnectionSearchResult>(std::move(search_result));
        }

        inline mathfp::Expected<ConnectionChoiceResult> run_validated_choice_step(
            ConnectionSearchResult search_result
            , const SearchParams& params
        ) {
            MATHFP_TRY_LET(
                ConnectionChoiceResult
                , choice_result
                , choose_connections(search_result, params)
            );
            MATHFP_TRY(validate_choice_step_output(choice_result, search_result));
            return mathfp::Expected<ConnectionChoiceResult>(std::move(choice_result));
        }

        inline mathfp::Expected<DemandSplitResult> run_validated_split_step(
            ConnectionChoiceResult choice_result
            , const InputModel& input
            , const SearchParams& params
        ) {
            MATHFP_TRY(validate_split_step_input(choice_result, input));
            MATHFP_TRY_LET(
                DemandSplitResult
                , split_result
                , split_demand_over_connections(choice_result, input, params)
            );
            MATHFP_TRY(validate_split_step_output(
                split_result
                , choice_result
                , input
            ));
            return mathfp::Expected<DemandSplitResult>(std::move(split_result));
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
            detail::run_validated_preprocessing_step(input)
            | and_then([&](PreprocessedNetwork net) {
                return detail::run_validated_search_step(std::move(net), input.params);
            })
            | and_then([&](ConnectionSearchResult search_result) {
                return detail::run_validated_choice_step(
                    std::move(search_result)
                    , input.params
                );
            })
            | and_then([&](ConnectionChoiceResult choice_result) {
                return detail::run_validated_split_step(
                    std::move(choice_result)
                    , input.input
                    , input.params
                );
            });
    }

}  // namespace timetable::domain::assignment
