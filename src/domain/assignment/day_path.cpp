#include "timetable/domain/assignment/day_path.hpp"

#include <utility>
#include <vector>

#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&        retention
        , SearchConnection         connection
        , const SearchCostContext& search_cost
        , IntervalId               interval
    ) {
        return retain_day_path_alternative(
              retention
            , std::move(connection)
            , search_cost
            , interval
            , DayPathRetentionConfig{}
        );
    }

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
    ) {
        return retain_day_path_alternative(
              retention
            , day_path_signature_of(connection)
            , std::move(connection)
            , search_cost
            , interval
            , config
        );
    }

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , DayPathSignature              signature
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
    ) {
        MATHFP_TRY_LET(
              CompleteConnectionMetrics
            , metrics
            , complete_connection_metrics(connection, search_cost, interval)
        );
        const auto connection_metrics = metrics_of(connection);

        auto it = retention.alternatives_by_signature.find(signature);
        if (it == retention.alternatives_by_signature.end()) {
            auto alternative = make_day_path_alternative_from_metrics(
                  std::move(connection)
                , std::move(signature)
                , metrics
                , connection_metrics
            );
            const auto key = alternative.identity.signature;
            retention.alternatives_by_signature.emplace(
                  key
                , std::move(alternative)
            );
            MATHFP_TRY(enforce_day_path_retention(retention, config));
            const auto retained =
                retention.alternatives_by_signature.find(key)
                    != retention.alternatives_by_signature.end();
            return DayPathRetentionDecision{
                  .inserted_path          = retained
                , .replaced_representative = retained
                , .timed_connection_count = retained ? 1u : 0u
            };
        }

        auto& alternative = it->second;
        auto support = make_day_path_support_descriptor(
              connection
            , signature
            , metrics
            , connection_metrics
        );
        const auto replaced = better_day_path_representative(
              metrics
            , alternative.support.representative_metrics
        );
        MATHFP_TRY(retain_day_path_support(
              alternative.support.split_support
            , std::move(support)
            , config
        ));
        if (replaced) {
            alternative.support.representative = std::move(connection);
            alternative.support.representative_metrics            = metrics;
            alternative.support.representative_connection_metrics = connection_metrics;
        }
        MATHFP_TRY(enforce_day_path_retention(retention, config));
        const auto retained = retention.alternatives_by_signature.find(signature);
        const auto support_count = retained != retention.alternatives_by_signature.end()
            ? retained->second.support.split_support.supports.size()
            : 0u;
        return DayPathRetentionDecision{
            .inserted_path          = false
          , .replaced_representative = replaced && retained != retention.alternatives_by_signature.end()
          , .timed_connection_count = support_count
        };
    }

    mathfp::Expected<DayPathAlternative> make_day_path_alternative(
          SearchConnection         connection
        , const SearchCostContext& search_cost
        , IntervalId               interval
    ) {
        auto signature = day_path_signature_of(connection);
        MATHFP_TRY_LET(
              CompleteConnectionMetrics
            , metrics
            , complete_connection_metrics(connection, search_cost, interval)
        );
        const auto connection_metrics = metrics_of(connection);
        return make_day_path_alternative_from_metrics(
              std::move(connection)
            , std::move(signature)
            , metrics
            , connection_metrics
        );
    }

    mathfp::Expected<std::vector<SearchConnection>>
    retain_day_path_representative_connections(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    ) {
        DayPathRetention retention;
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        return finalize_day_path_representatives(std::move(retention));
    }

    mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    ) {
        DayPathRetention retention;
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        return finalize_day_path_alternatives(std::move(retention));
    }

    mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const ChoiceTolerances&       tolerances
        , ChoiceRolloutStage            rollout_stage
    ) {
        DayPathRetention retention;
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        return finalize_day_path_alternatives(
              std::move(retention)
            , tolerances
            , rollout_stage
        );
    }

}  // namespace timetable::domain::assignment
