#include "timetable/domain/assignment/day_path.hpp"

#include <algorithm>
#include <utility>

#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] DayPathLeg day_path_leg_of(
            const ConnectionLeg& leg
        ) noexcept {
            return DayPathLeg{
                  .kind            = leg.kind
                , .route_segment   = leg.route_segment
                , .physical_from   = leg.physical_from
                , .physical_to     = leg.physical_to
                , .occurrence_from = leg.occurrence_from
                , .occurrence_to   = leg.occurrence_to
                , .line            = leg.line
                , .route           = leg.route
            };
        }

        [[nodiscard]] bool better_day_path_representative(
              const CompleteConnectionMetrics& candidate
            , const CompleteConnectionMetrics& current
        ) noexcept {
            if (complete_connection_dominates(candidate, current)) {
                return true;
            }
            if (complete_connection_dominates(current, candidate)) {
                return false;
            }

            if (candidate.impedance != current.impedance) {
                return candidate.impedance < current.impedance;
            }
            if (candidate.journey_time.value() != current.journey_time.value()) {
                return candidate.journey_time.value() < current.journey_time.value();
            }
            if (candidate.transfers.get() != current.transfers.get()) {
                return candidate.transfers.get() < current.transfers.get();
            }
            if (candidate.arrival.value() != current.arrival.value()) {
                return candidate.arrival.value() < current.arrival.value();
            }
            return candidate.departure.value() > current.departure.value();
        }

    }  // namespace

    DayPathSignature day_path_signature_of(
        const SearchConnection& connection
    ) {
        const auto& canonical = canonical_connection(connection);
        DayPathSignature signature{
              .origin      = canonical.origin
            , .destination = canonical.destination
            , .legs        = {}
        };
        signature.legs.reserve(canonical.trace.legs.size());
        for (const auto& leg : canonical.trace.legs) {
            if (is_wait_leg(leg.kind)) {
                continue;
            }
            signature.legs.push_back(day_path_leg_of(leg));
        }
        return signature;
    }

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&        retention
        , SearchConnection         connection
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

        auto it = std::find_if(
              retention.alternatives.begin()
            , retention.alternatives.end()
            , [&](const DayPathAlternative& alternative) {
                  return alternative.signature == signature;
              }
        );
        if (it == retention.alternatives.end()) {
            retention.alternatives.push_back(
                DayPathAlternative{
                      .signature                         = std::move(signature)
                    , .representative                    = std::move(connection)
                    , .representative_metrics            = metrics
                    , .representative_connection_metrics = connection_metrics
                    , .timed_connection_count            = 1u
                }
            );
            return DayPathRetentionDecision{
                  .inserted_path          = true
                , .replaced_representative = true
                , .timed_connection_count = 1u
            };
        }

        ++it->timed_connection_count;
        const auto replaced = better_day_path_representative(
              metrics
            , it->representative_metrics
        );
        if (replaced) {
            it->representative                    = std::move(connection);
            it->representative_metrics            = metrics;
            it->representative_connection_metrics = connection_metrics;
        }
        return DayPathRetentionDecision{
              .inserted_path          = false
            , .replaced_representative = replaced
            , .timed_connection_count = it->timed_connection_count
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
        return DayPathAlternative{
              .signature                         = std::move(signature)
            , .representative                    = std::move(connection)
            , .representative_metrics            = metrics
            , .representative_connection_metrics = connection_metrics
            , .timed_connection_count            = 1u
        };
    }

    DayPathMetrics day_path_metrics_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return DayPathMetrics{
              .complete               = alternative.representative_metrics
            , .representative         = alternative.representative_connection_metrics
            , .timed_connection_count = alternative.timed_connection_count
        };
    }

    std::vector<SearchConnection> finalize_day_path_representatives(
        DayPathRetention retention
    ) {
        std::sort(
              retention.alternatives.begin()
            , retention.alternatives.end()
            , [](const DayPathAlternative& lhs, const DayPathAlternative& rhs) {
                  return lhs.signature < rhs.signature;
              }
        );

        std::vector<SearchConnection> representatives;
        representatives.reserve(retention.alternatives.size());
        for (auto& alternative : retention.alternatives) {
            representatives.push_back(std::move(alternative.representative));
        }
        return representatives;
    }

    std::vector<SearchConnection> day_path_representative_connections(
        std::span<const DayPathAlternative> alternatives
    ) {
        std::vector<SearchConnection> representatives;
        representatives.reserve(alternatives.size());
        for (const auto& alternative : alternatives) {
            representatives.push_back(alternative.representative);
        }
        return representatives;
    }

    mathfp::Expected<std::vector<SearchConnection>>
    retain_day_path_representative_connections(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    ) {
        DayPathRetention retention;
        retention.alternatives.reserve(connections.size());
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
        retention.alternatives.reserve(connections.size());
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        std::sort(
              retention.alternatives.begin()
            , retention.alternatives.end()
            , [](const DayPathAlternative& lhs, const DayPathAlternative& rhs) {
                  return lhs.signature < rhs.signature;
              }
        );
        return std::move(retention.alternatives);
    }

}  // namespace timetable::domain::assignment
