#include "timetable/domain/assignment/steps.hpp"

#include <mathfp/core/error.hpp>

#include "timetable/domain/impedance.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , double fare_scale
        , const SearchParams& params
    ) {
        [[maybe_unused]] const auto& impedance = params.impedance;
        [[maybe_unused]] const auto normalized_fare_scale = fare_scale;

        // Placeholder: ensure impedance computation is wired in the search step.
        [[maybe_unused]] const auto branch_impedance = [&](Time journey_time,
            TransferCount transfers,
            const ConnectionSegment& segment) {
            return connection_impedance(
                journey_time
                , transfers
                , segment
                , impedance
                , normalized_fare_scale
            );
        };

        [[maybe_unused]] const auto successor_feasible = [&](
            const BranchState& state
            , const ConnectionSegment& successor
        ) {
            return is_branch_extension_feasible(
                state
                , successor
                , params.transfers
            );
        };

        // TODO: Implement branch-and-bound enumeration over PreprocessedNetwork.
        // TODO: Integrate allow_end_wait semantics at the destination/finalization stage.
        timetable::infra::progress::both(
            "search: branch-and-bound (not implemented)"
            , timetable::infra::LogLevel::Warning
        );
        return mathfp::unexpected(
            mathfp::not_implemented("connection search step not implemented yet")
        );
    }

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
        const ConnectionSearchResult& search_result
        , const SearchParams& params
    ) {
        (void)search_result;
        (void)params;
        // TODO: Implement final connection filtering using ChoiceTolerances and
        // dominance rules from the timetable-assignment model.
        timetable::infra::progress::both(
            "choice: pruning connections (not implemented)"
            , timetable::infra::LogLevel::Warning
        );
        return mathfp::unexpected(
            mathfp::not_implemented("connection choice step not implemented yet")
        );
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
        , const SearchParams& params
    ) {
        (void)choice_result;
        (void)input;
        (void)params;
        // TODO: Implement demand split over chosen connections according to the
        // Box-Cox / logit-style split model encoded in SplitParams.
        timetable::infra::progress::both(
            "split: demand assignment (not implemented)"
            , timetable::infra::LogLevel::Warning
        );
        return mathfp::unexpected(
            mathfp::not_implemented("demand split step not implemented yet")
        );
    }

}  // namespace timetable::domain::assignment
