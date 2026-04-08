#include "detail/output_internal.hpp"

#include <cstdint>
#include <map>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment::detail {

    namespace {

        using ChosenConnectionIndexMap = std::map<grouping::ConnectionTraceKey, std::size_t>;

        AssignmentOdResult make_empty_od_result(
            const grouping::OdKey& od
        ) {
            return AssignmentOdResult{
                  .origin                  = od.origin
                , .destination             = od.destination
                , .search_connection_count = 0
                , .chosen_connection_count = 0
                , .total_demand_passengers = 0.0
                , .assigned_passengers     = 0.0
                , .connections             = {}
                , .intervals               = {}
            };
        }

        double total_input_demand(
            const InputModel& input
        ) {
            double total = 0.0;
            for (const auto& demand : input.demand) {
                total += demand.passengers;
            }
            return total;
        }

        double total_assigned_passengers(
            const DemandSplitResult& split_result
        ) {
            double total = 0.0;
            for (const auto& share : split_result.shares) {
                total += share.passengers;
            }
            return total;
        }

        std::map<grouping::OdKey, bool> collect_all_ods(
              const std::map<grouping::OdKey, std::size_t>& search_counts
            , const grouping::BorrowedOdConnectionGroups&   chosen_by_od
            , const grouping::DemandEntryGroups&            demand_by_od
        ) {
            std::map<grouping::OdKey, bool> all_ods;
            for (const auto& [od, _] : search_counts) {
                all_ods.emplace(od, true);
            }
            for (const auto& [od, _] : chosen_by_od) {
                all_ods.emplace(od, true);
            }
            for (const auto& [od, _] : demand_by_od) {
                all_ods.emplace(od, true);
            }
            return all_ods;
        }

        AssignmentOutput::Summary build_output_summary(
              const InputModel&             input
            , const ConnectionSearchResult& search_result
            , const ConnectionChoiceResult& choice_result
            , const DemandSplitResult&      split_result
        ) {
            return AssignmentOutput::Summary{
                  .search_connection_count = search_result.connections.size()
                , .chosen_connection_count = choice_result.connections.size()
                , .demand_share_count      = split_result.shares.size()
                , .total_demand_passengers = total_input_demand(input)
                , .assigned_passengers     = total_assigned_passengers(split_result)
            };
        }

        void assign_search_connection_count(
              AssignmentOdResult&                           od_result
            , const std::map<grouping::OdKey, std::size_t>& search_counts
            , const grouping::OdKey&                        od
        ) {
            if (const auto search_it = search_counts.find(od); search_it != search_counts.end()) {
                od_result.search_connection_count = search_it->second;
            }
        }

        mathfp::Expected<ChosenConnectionIndexMap> append_chosen_connections(
              AssignmentOdResult&                         od_result
            , const PreprocessedNetwork&                  network
            , const grouping::BorrowedOdConnectionGroups& chosen_by_od
            , const grouping::OdKey&                      od
        ) {
            ChosenConnectionIndexMap chosen_connection_indices;
            const auto chosen_it = chosen_by_od.find(od);
            if (chosen_it == chosen_by_od.end()) {
                return chosen_connection_indices;
            }

            od_result.connections.reserve(chosen_it->second.size());
            for (const auto* connection : chosen_it->second) {
                MATHFP_TRY_LET(
                      AssignmentConnection
                    , mapped_connection
                    , build_assignment_connection(*connection, network)
                );
                const auto index = od_result.connections.size();
                chosen_connection_indices.emplace(grouping::connection_trace_key(*connection), index);
                od_result.connections.push_back(std::move(mapped_connection));
            }
            od_result.chosen_connection_count = od_result.connections.size();
            return chosen_connection_indices;
        }

        mathfp::Expected<AssignmentDemandInterval> build_assignment_interval(
              const InputModel&               input
            , const DemandEntry&              demand
            , const grouping::ShareGroups&    shares_by_key
            , const ChosenConnectionIndexMap& chosen_connection_indices
        ) {
            MATHFP_TRY_LET(
                  const TimeInterval*
                , interval
                , find_interval(input, demand.interval)
            );

            AssignmentDemandInterval interval_result{
                  .interval            = *interval
                , .demand_passengers   = demand.passengers
                , .assigned_passengers = 0.0
                , .shares              = {}
            };

            const auto shares_it = shares_by_key.find(grouping::demand_key(demand));
            if (shares_it == shares_by_key.end()) {
                return interval_result;
            }

            interval_result.shares.reserve(shares_it->second.size());
            for (const auto* share : shares_it->second) {
                const auto trace_key     = grouping::connection_trace_key(share->connection);
                const auto connection_it = chosen_connection_indices.find(trace_key);
                if (connection_it == chosen_connection_indices.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output mapping could not match split share to chosen connection")
                            .ctx("origin"     , share->origin     .get())
                            .ctx("destination", share->destination.get())
                            .ctx("interval_id", share->interval   .get())
                    );
                }

                interval_result.shares.push_back(
                    AssignmentIntervalShare{
                        .connection_index = AssignmentConnectionRef{
                            static_cast<std::int64_t>(connection_it->second)
                        }
                        , .passengers      = share->passengers
                        , .probability     = share->probability
                        , .independence    = share->independence
                        , .split_impedance = share->split_impedance
                    }
                );
                interval_result.assigned_passengers += share->passengers;
            }

            return interval_result;
        }

        mathfp::Expected<mathfp::Unit> append_demand_intervals(
              AssignmentOdResult&                od_result
            , const InputModel&                  input
            , const grouping::DemandEntryGroups& demand_by_od
            , const grouping::ShareGroups&       shares_by_key
            , const ChosenConnectionIndexMap&    chosen_connection_indices
            , const grouping::OdKey&             od
        ) {
            const auto demand_it = demand_by_od.find(od);
            if (demand_it == demand_by_od.end()) {
                return mathfp::kUnit;
            }

            od_result.intervals.reserve(demand_it->second.size());
            for (const auto* demand : demand_it->second) {
                od_result.total_demand_passengers += demand->passengers;

                MATHFP_TRY_LET(
                      AssignmentDemandInterval
                    , interval_result
                    , build_assignment_interval(
                          input
                        , *demand
                        , shares_by_key
                        , chosen_connection_indices
                    )
                );
                od_result.assigned_passengers += interval_result.assigned_passengers;
                od_result.intervals.push_back(std::move(interval_result));
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<AssignmentOdResult> build_assignment_od_result(
              const grouping::OdKey&                        od
            , const InputModel&                             input
            , const PreprocessedNetwork&                    network
            , const std::map<grouping::OdKey, std::size_t>& search_counts
            , const grouping::BorrowedOdConnectionGroups&   chosen_by_od
            , const grouping::DemandEntryGroups&            demand_by_od
            , const grouping::ShareGroups&                  shares_by_key
        ) {
            auto od_result = make_empty_od_result(od);
            assign_search_connection_count(od_result, search_counts, od);

            MATHFP_TRY_LET(
                  ChosenConnectionIndexMap
                , chosen_connection_indices
                , append_chosen_connections(
                      od_result
                    , network
                    , chosen_by_od
                    , od
                )
            );
            MATHFP_TRY(append_demand_intervals(
                  od_result
                , input
                , demand_by_od
                , shares_by_key
                , chosen_connection_indices
                , od
            ));

            return od_result;
        }

    }  // namespace

    mathfp::Expected<AssignmentOutput> build_assignment_output_impl(
          const InputModel&             input
        , const PreprocessedNetwork&    network
        , const ConnectionSearchResult& search_result
        , const ConnectionChoiceResult& choice_result
        , const DemandSplitResult&      split_result
    ) {
        const auto search_counts = grouping::count_connections_by_od(search_result.connections);
        const auto chosen_by_od  = grouping::group_connection_ptrs_by_od(choice_result.connections);
        const auto demand_by_od  = grouping::group_demand_entries_by_od(input.demand);
        const auto shares_by_key = grouping::group_shares_by_demand_key(split_result.shares);
        const auto all_ods       = collect_all_ods(search_counts, chosen_by_od, demand_by_od);

        AssignmentOutput output{
              .summary    = build_output_summary(input, search_result, choice_result, split_result)
            , .od_results = {}
        };
        output.od_results.reserve(all_ods.size());

        for (const auto& [od, _] : all_ods) {
            MATHFP_TRY_LET(
                  AssignmentOdResult
                , od_result
                , build_assignment_od_result(
                      od
                    , input
                    , network
                    , search_counts
                    , chosen_by_od
                    , demand_by_od
                    , shares_by_key
                )
            );
            output.od_results.push_back(std::move(od_result));
        }

        output.summary.od_count = output.od_results.size();
        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

}  // namespace timetable::domain::assignment::detail
