#include "timetable/domain/assignment/validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include "../detail/validation_common.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct OffsetValidationSpec final {
            std::span<const std::size_t> offsets{};
            std::size_t                  bucket_count{};
            std::size_t                  order_count{};
            std::string_view             name{};
        };

        mathfp::Expected<mathfp::Unit> validate_index_offsets(
              std::span<const std::size_t> offsets
            , std::size_t                  bucket_count
            , std::size_t                  order_count
            , std::string_view             name
        ) {
            if (offsets.size() != bucket_count + 1) {
                return mathfp::unexpected(
                    mathfp::internal_error("index offsets size mismatch")
                        .ctx("index"       , std::string(name))
                        .ctx("bucket_count", static_cast<std::int64_t>(bucket_count))
                        .ctx("offset_count", static_cast<std::int64_t>(offsets.size()))
                );
            }
            if (offsets.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("index offsets unexpectedly empty")
                        .ctx("index", std::string(name))
                );
            }
            if (offsets.front() != 0 || offsets.back() != order_count) {
                return mathfp::unexpected(
                    mathfp::internal_error("index offsets boundary mismatch")
                        .ctx("index"       , std::string(name))
                        .ctx("first_offset", static_cast<std::int64_t>(offsets.front()))
                        .ctx("last_offset" , static_cast<std::int64_t>(offsets.back()))
                        .ctx("order_count" , static_cast<std::int64_t>(order_count))
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] bool has_positive_fare(
            const ConnectionSegment& segment
        ) noexcept {
            return segment.fare.has_value()
                && std::isfinite(*segment.fare)
                && *segment.fare > 0.0;
        }

        mathfp::Expected<mathfp::Unit> validate_connection_admissibility_input(
              const ConnectionDeletionConfig& deletion
            , const DemandSegmentTimeConfig&  demand_time
        ) {
            MATHFP_TRY(validate_connection_admissibility_config(
                ConnectionAdmissibilityConfig{
                      .deletion    = deletion
                    , .demand_time = demand_time
                }
            ));

            if (demand_time.basis == DemandSegmentBasis::Departure
                && deletion.delete_arrivals_after_assignment_period_for_arrival_based) {
                detail::validation::warn(
                    "preprocessing input: arrival-based connection deletion flag is set but demand segment basis is departure"
                );
            }

            if (demand_time.basis == DemandSegmentBasis::Arrival
                && deletion.delete_departures_before_assignment_period_for_departure_based) {
                detail::validation::warn(
                    "preprocessing input: departure-based connection deletion flag is set but demand segment basis is arrival"
                );
            }

            if (demand_time.basis == DemandSegmentBasis::Arrival) {
                detail::validation::warn(
                    "preprocessing input: arrival-based demand segment filtering is enabled; split temporal utility will use arrival time as reference"
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_input(
            const AssignmentInput& input
        ) {
            MATHFP_TRY(validate_capacity_aware_assignment_config(
                input.capacity_aware_assignment
            ));
            MATHFP_TRY(validate_vehicle_journey_item_capacity_input(
                input.vehicle_journey_item_capacity
            ));

            const auto split_factor = mathfp::units::as_dimless(
                input.params.split.perceived_journey_time.volume_capacity_ratio
            );
            const auto search_factor = mathfp::units::as_dimless(
                input.params.impedance.volume_capacity_ratio
            );
            const auto search_enabled = input.capacity_aware_assignment.search_mode
                == CapacityAwareSearchMode::Enabled;
            const auto search_stored_only = input.capacity_aware_assignment.search_mode
                == CapacityAwareSearchMode::StoredOnly;

            if (split_factor > 0.0
                && !input.capacity_aware_assignment.capacity_aware_split_enabled) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("positive PerceivedJourneyTime.volCapRatioFactor requires enabled capacity-aware split")
                        .ctx("vol_cap_ratio_factor", split_factor)
                );
            }

            if (input.capacity_aware_assignment.capacity_aware_split_enabled
                && !(split_factor > 0.0)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("enabled capacity-aware split requires positive PerceivedJourneyTime.volCapRatioFactor")
                        .ctx("vol_cap_ratio_factor", split_factor)
                );
            }

            if (search_factor > 0.0 && !search_enabled && !search_stored_only) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("positive SearchImp.volCapRatioFactor requires enabled capacity-aware search or explicit stored-only mode")
                        .ctx("vol_cap_ratio_factor", search_factor)
                        .ctx(
                              "capacity_aware_search_mode"
                            , std::string(to_string(input.capacity_aware_assignment.search_mode))
                        )
                );
            }

            if (search_enabled && !(search_factor > 0.0)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("enabled capacity-aware search requires positive SearchImp.volCapRatioFactor")
                        .ctx("vol_cap_ratio_factor", search_factor)
                );
            }

            if ((split_factor > 0.0 || search_enabled)
                && input.vehicle_journey_item_capacity.status
                    != VehicleJourneyItemCapacityInputStatus::Loaded) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("behavioral capacity-aware assignment requires loaded vehicle journey item capacity input")
                        .ctx(
                              "capacity_input_status"
                            , std::string(to_string(input.vehicle_journey_item_capacity.status))
                          )
                );
            }

            if (search_stored_only && search_factor > 0.0) {
                detail::validation::warn(
                    "preprocessing input: SearchImp.volCapRatioFactor is stored but not applied because capacity-aware search mode is stored_only"
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_input(
        const AssignmentInput& input
    ) {
        const auto has_presegmented = input.presegmented.has_value();
        const auto has_raw_routes   = !input.input.routes    .empty();
        const auto has_raw_trips    = !input.input.trips     .empty();
        const auto has_walk_links   = !input.input.walk_links.empty();

        if (!has_presegmented && !has_raw_routes && !has_raw_trips && !has_walk_links) {
            return mathfp::unexpected(
                mathfp::invalid_arg("assignment input contains no supply data")
            );
        }

        if (has_presegmented && (has_raw_routes || has_raw_trips || has_walk_links)) {
            detail::validation::warn(
                "preprocessing input: presegmented supply is present; raw routes/trips/walk_links will be ignored"
            );
        }

        if (input.input.intervals.empty() && input.input.demand.empty()) {
            detail::validation::warn(
                "preprocessing input: no intervals or demand entries loaded; full pipeline will fail at split stage"
            );
        } else if (input.input.intervals.empty()) {
            detail::validation::warn(
                "preprocessing input: demand entries are present without time intervals; split stage will fail"
            );
        } else if (input.input.demand.empty()) {
            detail::validation::warn(
                "preprocessing input: time intervals are present without demand entries; split stage will fail"
            );
        }

        MATHFP_TRY(validate_skim_matrix_config(input.skim_matrix));
        MATHFP_TRY(validate_assignment_execution_config(input.execution));
        MATHFP_TRY(validate_capacity_aware_assignment_input(input));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            input.complete_connection_dominance
        ));
        MATHFP_TRY(validate_assignment_period_config(input.assignment_period));
        MATHFP_TRY(validate_connection_admissibility_input(
              input.connection_deletion
            , input.demand_segment_time
        ));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_output(
          const PreprocessedNetwork& network
        , const SearchParams&        params
    ) {
        if (network.route_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("preprocessing produced no route segments")
            );
        }
        if (network.connection_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("preprocessing produced no connection segments")
            );
        }

        constexpr auto kOffsetSpecCount = std::size_t{ 5 };
        const std::array<OffsetValidationSpec, kOffsetSpecCount> offset_specs{{
              OffsetValidationSpec{
                    .offsets      = network.route_index.line_offsets
                  , .bucket_count = network.route_index.line_buckets.size()
                  , .order_count  = network.route_index.line_order.size()
                  , .name         = "route_index.line"
              }
            , OffsetValidationSpec{
                    .offsets      = network.route_index.walk_offsets
                  , .bucket_count = network.route_index.walk_buckets.size()
                  , .order_count  = network.route_index.walk_order.size()
                  , .name         = "route_index.walk"
              }
            , OffsetValidationSpec{
                    .offsets      = network.connection_index.timed_offsets
                  , .bucket_count = network.connection_index.timed_buckets.size()
                  , .order_count  = network.connection_index.timed_order.size()
                  , .name         = "connection_index.timed"
              }
            , OffsetValidationSpec{
                    .offsets      = network.connection_index.boarding_offsets
                  , .bucket_count = network.connection_index.boarding_stop_buckets.size()
                  , .order_count  = network.connection_index.boarding_order.size()
                  , .name         = "connection_index.boarding"
              }
            , OffsetValidationSpec{
                    .offsets      = network.connection_index.walk_offsets
                  , .bucket_count = network.connection_index.walk_buckets.size()
                  , .order_count  = network.connection_index.walk_order.size()
                  , .name         = "connection_index.walk"
              }
        }};
        MATHFP_TRY(detail::validation::validate_each_index(
              offset_specs
            , [](const OffsetValidationSpec& spec, std::size_t) {
                return validate_index_offsets(
                      spec.offsets
                    , spec.bucket_count
                    , spec.order_count
                    , spec.name
                );
            }
        ));

        if (network.route_index.line_order.size() + network.route_index.walk_order.size()
            != network.route_segments.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("route index partition does not cover all route segments")
                    .ctx("line_count"   , static_cast<std::int64_t>(network.route_index.line_order.size()))
                    .ctx("walk_count"   , static_cast<std::int64_t>(network.route_index.walk_order.size()))
                    .ctx("segment_count", static_cast<std::int64_t>(network.route_segments        .size()))
            );
        }
        if (network.connection_index.timed_order.size() + network.connection_index.walk_order.size()
            != network.connection_segments.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("connection index partition does not cover all connection segments")
                    .ctx("timed_count"  , static_cast<std::int64_t>(network.connection_index.timed_order.size()))
                    .ctx("walk_count"   , static_cast<std::int64_t>(network.connection_index.walk_order .size()))
                    .ctx("segment_count", static_cast<std::int64_t>(network.connection_segments         .size()))
            );
        }
        if (network.connection_index.boarding_order.size() != network.connection_index.timed_order.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("boarding index size does not match timed connection count")
                    .ctx("boarding_count", static_cast<std::int64_t>(network.connection_index.boarding_order.size()))
                    .ctx("timed_count"   , static_cast<std::int64_t>(network.connection_index.timed_order   .size()))
            );
        }

        const auto has_zone_bucket = std::any_of(
              network.connection_index.walk_buckets.begin()
            , network.connection_index.walk_buckets.end()
            , [](const EndpointKey& key) { return key.kind == EndpointKind::Zone; }
        );
        if (!has_zone_bucket) {
            return mathfp::unexpected(
                mathfp::invalid_arg("preprocessing produced no zone endpoints for search origins/destinations")
            );
        }

        const auto fare_required = mathfp::units::as_dimless(params.impedance.fare) > 0.0;
        const auto has_any_positive_fare = std::any_of(
              network.connection_segments.begin()
            , network.connection_segments.end()
            , has_positive_fare
        );
        if (fare_required && !has_any_positive_fare) {
            detail::validation::warn(
                "preprocessing output: fare weight is positive but no connection segment has positive fare; fare term will collapse to zero"
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
