#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Semantic kind of one time-realized connection leg.
     *
     * A canonical connection trace contains both movement and waiting legs.
     * Waiting is explicit because search, choice and split models may weight
     * initial, transfer and final waiting differently from walking and riding.
     */
    enum class ConnectionLegKind : std::uint8_t {
          AccessWalk
        , InitialWait
        , Ride
        , TransferWait
        , TransferWalk
        , EgressWalk
        , FinalWait
    };

    inline constexpr std::array kConnectionLegKindTokens{
          timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::AccessWalk, "access_walk"
          }
        , timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::InitialWait, "initial_wait"
          }
        , timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::Ride, "ride"
          }
        , timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::TransferWait, "transfer_wait"
          }
        , timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::TransferWalk, "transfer_walk"
          }
        , timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::EgressWalk, "egress_walk"
          }
        , timetable::EnumStringEntry<ConnectionLegKind>{
              ConnectionLegKind::FinalWait, "final_wait"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        ConnectionLegKind value
    ) noexcept {
        return timetable::enum_to_string(value, kConnectionLegKindTokens);
    }

    [[nodiscard]] inline constexpr std::optional<ConnectionLegKind> connection_leg_kind_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kConnectionLegKindTokens);
    }

    [[nodiscard]] inline constexpr bool is_walk_leg(
        ConnectionLegKind kind
    ) noexcept {
        return kind == ConnectionLegKind::AccessWalk
            || kind == ConnectionLegKind::TransferWalk
            || kind == ConnectionLegKind::EgressWalk;
    }

    [[nodiscard]] inline constexpr bool is_wait_leg(
        ConnectionLegKind kind
    ) noexcept {
        return kind == ConnectionLegKind::InitialWait
            || kind == ConnectionLegKind::TransferWait
            || kind == ConnectionLegKind::FinalWait;
    }

    [[nodiscard]] inline constexpr bool is_ride_leg(
        ConnectionLegKind kind
    ) noexcept {
        return kind == ConnectionLegKind::Ride;
    }

    /**
     * @brief One time-realized leg of a passenger connection.
     *
     * connection_segment and route_segment are present for supply-backed legs
     * (walk and ride). They are absent for pure waiting legs.
     *
     * physical_from/physical_to live in the stop-zone endpoint space. The
     * occurrence fields are populated for ride legs only, where route-position
     * occurrences are required for loop lines and precise load projection.
     */
    struct ConnectionLeg final {
        ConnectionLegKind                  kind{};
        std::optional<ConnectionSegmentId> connection_segment{};
        std::optional<RouteSegmentId>      route_segment{};

        EndpointKey                        physical_from{};
        EndpointKey                        physical_to{};
        std::optional<StopOccurrenceKey>   occurrence_from{};
        std::optional<StopOccurrenceKey>   occurrence_to{};

        std::optional<LineId>              line{};
        std::optional<TripId>              trip{};

        Time                               start_time{};
        Time                               end_time{};
        Length                             length{};
        double                             fare{};
    };

    /**
     * @brief Ordered time-realized passenger trajectory.
     *
     * The trace is the canonical structural object. Metrics and all behavioral
     * evaluations must be derived from it instead of being treated as primary
     * facts.
     */
    struct ConnectionTrace final {
        std::vector<ConnectionLeg> legs{};
    };

    /**
     * @brief Parameter-independent aggregate properties of a connection trace.
     *
     * These values are invariants of the realized trace. Search impedance,
     * choice filters, perceived journey time and split impedance are functions
     * of these metrics and external model parameters; they are intentionally not
     * stored here.
     */
    struct ConnectionMetrics final {
        Time         departure_time{};
        Time         arrival_time{};
        Time         journey_time{};

        Time         access_time{};
        Time         initial_wait_time{};
        Time         in_vehicle_time{};
        Time         transfer_wait_time{};
        Time         transfer_walk_time{};
        Time         egress_time{};
        Time         final_wait_time{};

        Time         walk_time{};
        Time         wait_time{};

        Length       in_vehicle_distance{};
        Length       walk_distance{};
        Length       journey_distance{};

        std::int32_t ride_count{};
        TransferCount transfer_count{};
        double       fare{};
    };

    /**
     * @brief Canonical connection between one origin and one destination zone.
     *
     * A connection is only the OD-bound time-realized trace. Metrics and every
     * impedance-like value are derived projections layered on top of this
     * structural object.
     */
    struct Connection final {
        ZoneId            origin{};
        ZoneId            destination{};
        ConnectionTrace   trace{};
    };

    /**
     * @brief Validate trace-level invariants independent of an OD pair.
     *
     * The trace must be non-empty, time-contiguous, endpoint-contiguous and
     * semantically consistent with every leg kind. In particular, pure waiting
     * legs must be explicit zero-distance same-endpoint legs.
     */
    mathfp::Expected<mathfp::Unit> validate_connection_trace(
        const ConnectionTrace& trace
    );

    /**
     * @brief Derive parameter-independent metrics from a canonical trace.
     *
     * This is the only supported direction: metrics are consequences of legs,
     * not independently authored facts.
     */
    mathfp::Expected<ConnectionMetrics> compute_connection_metrics(
        const ConnectionTrace& trace
    );

    /**
     * @brief Derive parameter-independent metrics from a canonical connection.
     */
    mathfp::Expected<ConnectionMetrics> compute_connection_metrics(
        const Connection& connection
    );

    /**
     * @brief Construct an OD-bound canonical connection from a validated trace.
     */
    mathfp::Expected<Connection> make_connection(
          ZoneId          origin
        , ZoneId          destination
        , ConnectionTrace trace
    );

}  // namespace timetable::domain::assignment
