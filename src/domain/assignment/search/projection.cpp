#include "timetable/domain/assignment/search/projection.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] DayPathLeg make_day_path_walk_leg(
              ConnectionLegKind   kind
            , const RouteSegment& route_segment
        ) noexcept {
            return DayPathLeg{
                  .kind            = kind
                , .route_segment   = route_segment.id
                , .physical_from   = physical_from_key(route_segment)
                , .physical_to     = physical_to_key(route_segment)
                , .occurrence_from = std::nullopt
                , .occurrence_to   = std::nullopt
                , .line            = std::nullopt
                , .route           = std::nullopt
            };
        }

        [[nodiscard]] DayPathLeg make_day_path_ride_leg(
            const RouteSegment& route_segment
        ) noexcept {
            const auto* line = line_topology_of(route_segment);
            return DayPathLeg{
                  .kind            = ConnectionLegKind::Ride
                , .route_segment   = route_segment.id
                , .physical_from   = physical_from_key(route_segment)
                , .physical_to     = physical_to_key(route_segment)
                , .occurrence_from = occurrence_key(line->from)
                , .occurrence_to   = occurrence_key(line->to)
                , .line            = line->line
                , .route           = line->route
            };
        }

    }  // namespace

    SearchProjectionSlot make_demand_task_projection_slot(
          const SearchTask& task
        , std::size_t       result_index
    ) noexcept {
        return SearchProjectionSlot{
              .kind         = SearchProjectionSlotKind::DemandTask
            , .origin       = task.origin
            , .destination  = task.destination
            , .interval     = task.interval.id
            , .task_ref     = task.index
            , .task         = std::cref(task)
            , .result_index = result_index
        };
    }

    mathfp::Expected<std::vector<SearchProjectionSlot>> build_demand_projection_slots(
        std::span<const SearchTask> tasks
    ) {
        std::vector<SearchProjectionSlot> slots;
        slots.reserve(tasks.size());
        std::map<SearchTaskRef, mathfp::Unit> seen;
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const auto [_, inserted] = seen.emplace(tasks[i].index, mathfp::kUnit);
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate search task ref while building demand projection slots")
                        .ctx("task", tasks[i].index.get())
                );
            }
            slots.push_back(make_demand_task_projection_slot(tasks[i], i));
        }
        return slots;
    }

    std::vector<SearchProjectionSlot> build_completion_target_projection_slots(
        const SearchTreeJob& job
    ) {
        std::vector<SearchProjectionSlot> slots;
        slots.reserve(job.completion_targets.size());
        for (const auto& target : job.completion_targets) {
            slots.push_back(
                SearchProjectionSlot{
                      .kind              = SearchProjectionSlotKind::CompletionTarget
                    , .origin            = job.origin
                    , .destination       = target.destination
                    , .interval          = std::nullopt
                    , .result_index      = std::nullopt
                    , .completion_target = target.index
                }
            );
        }
        return slots;
    }

    std::vector<SearchProjectionSlot> build_od_day_pair_projection_slots(
        const SearchTreeJob& job
    ) {
        std::vector<SearchProjectionSlot> slots;
        slots.reserve(job.completion_targets.size());
        for (const auto& target : job.completion_targets) {
            slots.push_back(
                SearchProjectionSlot{
                      .kind              = SearchProjectionSlotKind::OdDayPair
                    , .origin            = job.origin
                    , .destination       = target.destination
                    , .interval          = std::nullopt
                    , .result_index      = std::nullopt
                    , .completion_target = target.index
                }
            );
        }
        return slots;
    }

    OdDayProductionCarrier project_od_day_carrier_transition(
          const OdDayProductionCarrier& carrier
        , const SearchSuccessor&        successor
        , const ConnectionSegment&      connection
        , const RouteSegment&           route_segment
    ) {
        auto path_identity = carrier.path_identity;
        auto support_envelope = carrier.support_envelope;
        if (successor.walk_transition.has_value()) {
            path_identity = append_od_day_path_leg(
                  std::move(path_identity)
                , make_day_path_walk_leg(
                      successor.walk_transition->kind
                    , route_segment
                  )
            );
        } else {
            path_identity = append_od_day_path_leg(
                  std::move(path_identity)
                , make_day_path_ride_leg(route_segment)
            );
            support_envelope = successor.support_envelope.value_or(
                propagate_timed_support_envelope(connection, route_segment)
            );
        }

        return OdDayProductionCarrier{
              .path_identity    = std::move(path_identity)
            , .support_envelope = std::move(support_envelope)
            , .support_prefix   = append_od_day_support_segment(
                  carrier.support_prefix
                , connection.id
            )
        };
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_post_layer_retention(
          const SearchProjectionSlot&       slot
        , const SearchProjectionRetention&  retention
    ) {
        if (slot.kind != SearchProjectionSlotKind::OdDayPair) {
            return mathfp::kUnit;
        }
        if (!retention.complete_connections.alternatives.empty()
            || !retention.compact_complete_connections.metrics.empty()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day post-layer retained raw complete alternatives")
                    .ctx("origin", slot.origin.get())
                    .ctx("destination", slot.destination.get())
                    .ctx(
                          "raw_complete"
                        , static_cast<std::int64_t>(
                              retention.complete_connections.alternatives.size()
                          )
                      )
                    .ctx(
                          "compact_complete"
                        , static_cast<std::int64_t>(
                              retention.compact_complete_connections.metrics.size()
                          )
                      )
            );
        }
        for (const auto& [signature, alternative] : retention.day_paths.alternatives_by_signature) {
            if (signature.origin != slot.origin
                || signature.destination != slot.destination) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day post-layer retained path outside projection slot")
                        .ctx("slot_origin", slot.origin.get())
                        .ctx("slot_destination", slot.destination.get())
                        .ctx("path_origin", signature.origin.get())
                        .ctx("path_destination", signature.destination.get())
                );
            }
            if (alternative.identity.signature != signature) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day post-layer path key and alternative identity disagree")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_post_layer_result(
        const SearchSlotResult& result
    ) {
        if (result.slot.kind != SearchProjectionSlotKind::OdDayPair) {
            return mathfp::kUnit;
        }
        if (!result.connections.empty()
            || result.connection_count != result.day_path_alternatives.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production slot result must contain only DayPath alternatives")
                    .ctx("origin", result.slot.origin.get())
                    .ctx("destination", result.slot.destination.get())
                    .ctx("connections", static_cast<std::int64_t>(result.connections.size()))
                    .ctx(
                          "day_paths"
                        , static_cast<std::int64_t>(result.day_path_alternatives.size())
                      )
            );
        }
        for (std::size_t i = 0; i < result.day_path_alternatives.size(); ++i) {
            const auto& alternative = result.day_path_alternatives[i];
            const auto& signature = alternative.identity.signature;
            if (signature.origin != result.slot.origin
                || signature.destination != result.slot.destination) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day result path outside projection slot")
                        .ctx("slot_origin", result.slot.origin.get())
                        .ctx("slot_destination", result.slot.destination.get())
                        .ctx("path_origin", signature.origin.get())
                        .ctx("path_destination", signature.destination.get())
                        .ctx("alternative", static_cast<std::int64_t>(i))
                );
            }
            if (alternative.support.split_support.supports.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day result path has no timed split support")
                        .ctx("origin", result.slot.origin.get())
                        .ctx("destination", result.slot.destination.get())
                        .ctx("alternative", static_cast<std::int64_t>(i))
                );
            }
        }
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
