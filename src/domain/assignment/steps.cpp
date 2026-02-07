#include "timetable/domain/assignment/steps.hpp"

#include <algorithm>
#include <cmath>
#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/domain/impedance.hpp"
#include "timetable/domain/segments_order.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network(
        const InputModel& input
        , const PreprocessParams& params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::both;

        both("preprocessing: building route segments");
        log(
            fmt::format(
                "input sizes: stops = {:>6}  zones = {:>6}  lines = {:>6}  routes = {:>6}\n"
                "             trips = {:>6}  walk_links = {:>6}  intervals = {:>6}  demand = {:>6}"
                , input.stops.size()
                , input.zones.size()
                , input.lines.size()
                , input.routes.size()
                , input.trips.size()
                , input.walk_links.size()
                , input.intervals.size()
                , input.demand.size()
            ),
            LogLevel::Info
        );

        MATHFP_TRY_LET(
            std::vector<RouteSegment>
            , line_segments
            , preprocessing::build_line_route_segments(
                input.routes, input.trips, input.stops, params
            )
        );
        both("preprocessing: building walk segments");
        MATHFP_TRY_LET(
            std::vector<RouteSegment>
            , walk_segments
            , preprocessing::build_walk_route_segments(
                input.walk_links, params
            )
        );
        log(
            fmt::format(
                "route segments: line = {:>8}  walk = {:>8}  total = {:>8}"
                , line_segments.size()
                , walk_segments.size()
                , line_segments.size() + walk_segments.size()
            ),
            LogLevel::Info
        );

        std::vector<RouteSegment> route_segments;
        route_segments.reserve(line_segments.size() + walk_segments.size());
        route_segments.insert(
            route_segments.end()
            , std::make_move_iterator(line_segments.begin())
            , std::make_move_iterator(line_segments.end())
        );
        route_segments.insert(
            route_segments.end()
            , std::make_move_iterator(walk_segments.begin())
            , std::make_move_iterator(walk_segments.end())
        );
        if (params.stable_ordering) {
            std::sort(route_segments.begin(), route_segments.end(), route_segment_less);
        }
        log(
            fmt::format(
                "route segments: stable_ordering = {}"
                , params.stable_ordering ? "true" : "false"
            ),
            LogLevel::Info
        );
        MATHFP_TRY(validate_route_segments(route_segments, true));

        reindex_route_segments(route_segments);

        both("preprocessing: building connection segments");
        MATHFP_TRY_LET(
            std::vector<ConnectionSegment>
            , connection_segments
            , preprocessing::build_connection_segments(
                route_segments, input.routes, input.trips, params
            )
        );
        log(
            fmt::format(
                "connection segments: total = {:>8}"
                , connection_segments.size()
            ),
            LogLevel::Info
        );
        both("preprocessing: building indices");
        MATHFP_TRY_LET(
            preprocessing::RouteSegmentIndex
            , route_index
            , preprocessing::build_route_segment_index(route_segments)
        );
        MATHFP_TRY_LET(
            preprocessing::ConnectionSegmentIndex
            , connection_index
            , preprocessing::build_connection_segment_index(
                connection_segments, route_segments
            )
        );
        log(
            fmt::format(
                "indices: route_order = {:>8}  route_buckets = {:>6}\n"
                "         timed_order = {:>8}  timed_buckets = {:>6}\n"
                "         walk_order  = {:>8}  walk_buckets  = {:>6}"
                , route_index.order.size()
                , route_index.buckets.size()
                , connection_index.timed_order.size()
                , connection_index.timed_buckets.size()
                , connection_index.walk_order.size()
                , connection_index.walk_buckets.size()
            ),
            LogLevel::Info
        );
        both("preprocessing: done");

        return PreprocessedNetwork{
            .route_segments        = std::move(route_segments)
            , .connection_segments = std::move(connection_segments)
            , .route_index         = std::move(route_index)
            , .connection_index    = std::move(connection_index)
            , .fare_scale          = 1.0
        };
    }

    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network_from_segments(
        std::vector<RouteSegment> route_segments
        , std::vector<ConnectionSegment> connection_segments
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: using presegmented data");
        log(
            fmt::format(
                "presegmented sizes: route_segments = {:>8}  connection_segments = {:>8}"
                , route_segments.size()
                , connection_segments.size()
            ),
            LogLevel::Info
        );

        if (route_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("route segments collection is empty")
            );
        }

        MATHFP_TRY_LET(
            preprocessing::RouteSegmentIndex
            , route_index
            , preprocessing::build_route_segment_index(route_segments)
        );
        MATHFP_TRY_LET(
            preprocessing::ConnectionSegmentIndex
            , connection_index
            , preprocessing::build_connection_segment_index(
                connection_segments, route_segments
            )
        );

        both("preprocessing: presegmented indexing done");

        return PreprocessedNetwork{
            .route_segments        = std::move(route_segments)
            , .connection_segments = std::move(connection_segments)
            , .route_index         = std::move(route_index)
            , .connection_index    = std::move(connection_index)
            , .fare_scale          = 1.0
        };
    }

    void reindex_route_segments(std::vector<RouteSegment>& segments) {
        for (std::size_t i = 0; i < segments.size(); ++i) {
            segments[i].id = RouteSegmentId{ static_cast<std::int64_t>(i) };
        }
    }

    mathfp::Expected<mathfp::Unit> validate_route_segments(
        const std::vector<RouteSegment>& segments
        , bool allow_empty
    ) {
        if (segments.empty() && !allow_empty) {
            return mathfp::unexpected(
                mathfp::invalid_arg("route segments collection is empty")
            );
        }

        struct Key final {
            EndpointKey             from{};
            EndpointKey             to{};
            CarrierKind             kind{};
            std::int64_t            carrier_id{};
            std::vector<WalkLinkId> path{};

            auto operator<=>(const Key&) const = default;
        };

        std::vector<Key> keys;
        keys.reserve(segments.size());
        for (const auto& s : segments) {
            const auto kind = carrier_kind(s.carrier);
            const auto carrier_id = is_line(s.carrier)
                ? std::get<LineId>(s.carrier).get()
                : 0;
            std::vector<WalkLinkId> path{};
            if (is_walk(s.carrier)) {
                path = std::get<WalkPath>(s.carrier);
            }
            keys.push_back(Key{
                .from = to_endpoint_key(s.from),
                .to = to_endpoint_key(s.to),
                .kind = kind,
                .carrier_id = carrier_id,
                .path = std::move(path)
            });
        }

        std::sort(keys.begin(), keys.end());
        const auto dup = std::adjacent_find(keys.begin(), keys.end());
        if (dup != keys.end()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("duplicate route segments detected")
                .ctx("from", dup->from.id)
                .ctx("to", dup->to.id)
                .ctx("carrier_id", dup->carrier_id)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , const SearchParams& params
    ) {
        [[maybe_unused]] const auto& impedance = params.impedance;
        [[maybe_unused]] const auto fare_scale = network.fare_scale;

        // Placeholder: ensure impedance computation is wired in the search step.
        [[maybe_unused]] const auto branch_impedance = [&](Time journey_time,
            TransferCount transfers,
            const ConnectionSegment& segment) {
            return connection_impedance(
                journey_time,
                transfers,
                segment,
                impedance,
                fare_scale
            );
        };

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
        timetable::infra::progress::both(
            "split: demand assignment (not implemented)"
            , timetable::infra::LogLevel::Warning
        );
        return mathfp::unexpected(
            mathfp::not_implemented("demand split step not implemented yet")
        );
    }

    double compute_fare_scale(
        std::span<const ConnectionSegment> segments
        , const FareNormalization& normalization
    ) {
        using Kind = FareNormalization::Kind;
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        if (normalization.kind == Kind::None) {
            log("fare normalization: disabled", LogLevel::Info);
            return 1.0;
        }
        if (normalization.kind == Kind::FixedScale) {
            log("fare normalization: fixed scale", LogLevel::Info);
            return normalization.fixed_scale > 0.0 ? normalization.fixed_scale : 1.0;
        }

        std::vector<double> fares;
        fares.reserve(segments.size());
        for (const auto& s : segments) {
            if (s.fare && std::isfinite(*s.fare)) {
                fares.push_back(*s.fare);
            }
        }
        if (fares.empty()) {
            log("fare normalization: no fares present; scale = 1", LogLevel::Info);
            return 1.0;
        }

        if (normalization.kind == Kind::Mean) {
            double sum = 0.0;
            for (const auto v : fares) sum += v;
            const auto scale = sum / static_cast<double>(fares.size());
            log(fmt::format("fare normalization: mean scale = {:.6f}", scale), LogLevel::Info);
            return scale;
        }

        auto nth = [&](std::size_t idx) {
            std::nth_element(fares.begin(), fares.begin() + static_cast<std::ptrdiff_t>(idx), fares.end());
            return fares[idx];
        };

        if (normalization.kind == Kind::Median) {
            const auto mid = fares.size() / 2;
            if (fares.size() % 2 == 1) {
                const auto scale = nth(mid);
                log(fmt::format("fare normalization: median scale = {:.6f}", scale), LogLevel::Info);
                return scale;
            }
            const auto upper = nth(mid);
            const auto lower = nth(mid - 1);
            const auto scale = 0.5 * (lower + upper);
            log(fmt::format("fare normalization: median scale = {:.6f}", scale), LogLevel::Info);
            return scale;
        }

        if (normalization.kind == Kind::P95) {
            const auto idx = static_cast<std::size_t>(std::floor(0.95 * (fares.size() - 1)));
            const auto scale = nth(idx);
            log(fmt::format("fare normalization: p95 scale = {:.6f}", scale), LogLevel::Info);
            return scale;
        }

        return 1.0;
    }

}  // namespace timetable::domain::assignment
