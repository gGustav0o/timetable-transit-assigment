#include "timetable/domain/assignment/search/relations/paper_connection_relevance.hpp"

#include <algorithm>
#include <iterator>

namespace timetable::domain::assignment {

    bool paper_node_connection_relevant(
          const ConnectionSetCy&        set
        , const ExactPruningPolicy&     policy
        , const SearchPruningMetrics&   candidate
    ) noexcept {
        const auto prefix_end = std::upper_bound(
              set.entries_.begin()
            , set.entries_.end()
            , candidate.arrival.value()
            , [](double arrival_value, const ConnectionSetCyEntry& rhs) {
                return arrival_value < rhs.metrics.arrival.value();
            }
        );

        auto prefix_count = static_cast<std::size_t>(
            std::distance(set.entries_.begin(), prefix_end)
        );
        while (prefix_count > 0u) {
            --prefix_count;
            if (dominates_exactly(policy, set.entries_[prefix_count].metrics, candidate)) {
                return false;
            }
        }
        return true;
    }

    SearchPruningDecision evaluate_paper_node_connection_set(
          const SearchPruningExecutionPlan& execution
        , const SearchPruningMetrics&       candidate
        , const ConnectionSetCy&            set
        , const TransferLimits&             limits
    ) noexcept {
        if (execution.exact_enabled
            && !paper_node_connection_relevant(
                  set
                , execution.exact_policy
                , candidate
            )) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = SearchPruningReason::RejectedExactDominance
                , .accepted = false
            };
        }

        if (execution.approximate_enabled
            && execution.approximate_policy.has_value()
            && !within_approximate_retention(
                  candidate
                , set.summary()
                , *execution.approximate_policy
                , limits
            )) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Approximate
                , .reason   = SearchPruningReason::RejectedApproximateTolerance
                , .accepted = false
            };
        }

        return SearchPruningDecision{
              .layer = execution.approximate_enabled
                  ? SearchPruningLayer::Approximate
                  : SearchPruningLayer::Exact
            , .reason   = SearchPruningReason::Accepted
            , .accepted = true
        };
    }

}  // namespace timetable::domain::assignment
