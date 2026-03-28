#include "timetable/domain/preprocessing/segments_index.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/domain/segment_semantics.hpp"
#include "timetable/domain/segments_order.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::preprocessing {

    namespace {

        struct RouteSegRef final {
            RouteSegmentId      id{};
            const RouteSegment* ptr{};
        };

        struct ConnectionSegRef final {
            ConnectionSegmentId      id{};
            const ConnectionSegment* ptr{};
            const RouteSegment*      route{};
        };

        struct PartitionedRouteRefs final {
            std::vector<RouteSegRef> line{};
            std::vector<RouteSegRef> walk{};
        };

        struct PartitionedConnectionRefs final {
            std::vector<ConnectionSegRef> timed{};
            std::vector<ConnectionSegRef> walk{};
        };

        template <class Key>
        struct BucketIndex final {
            std::vector<Key>         buckets{};
            std::vector<std::size_t> offsets{};
        };

        struct LineRouteIndexData final {
            std::vector<RouteSegmentId>    order{};
            BucketIndex<StopOccurrenceKey> bucket_index{};
        };

        struct WalkRouteIndexData final {
            std::vector<RouteSegmentId> order{};
            BucketIndex<EndpointKey>    bucket_index{};
        };

        struct TimedConnectionIndexData final {
            std::vector<ConnectionSegmentId> order{};
            std::vector<Time>                departures{};
            BucketIndex<StopOccurrenceKey>   bucket_index{};
        };

        struct BoardingConnectionIndexData final {
            std::vector<ConnectionSegmentId> order{};
            std::vector<Time>                departures{};
            BucketIndex<StopId>              bucket_index{};
        };

        struct WalkConnectionIndexData final {
            std::vector<ConnectionSegmentId> order{};
            BucketIndex<EndpointKey>         bucket_index{};
        };

        bool route_seg_ref_less(const RouteSegRef& a, const RouteSegRef& b) {
            return a.id.get() < b.id.get();
        }

        bool line_route_ref_less(const RouteSegRef& a, const RouteSegRef& b) {
            const auto* a_line = line_topology_of(*a.ptr);
            const auto* b_line = line_topology_of(*b.ptr);

            if (const auto a_from = occurrence_key(a_line->from), b_from = occurrence_key(b_line->from); a_from != b_from)
                return a_from < b_from;

            if (const auto a_to = occurrence_key(a_line->to), b_to = occurrence_key(b_line->to); a_to != b_to)
                return a_to < b_to;

            if (a_line->line != b_line->line)
                return a_line->line.get() < b_line->line.get();

            return a.id.get() < b.id.get();
        }

        bool walk_route_ref_less(const RouteSegRef& a, const RouteSegRef& b) {
            return route_segment_less(*a.ptr, *b.ptr);
        }

        bool timed_ref_less(const ConnectionSegRef& a, const ConnectionSegRef& b) {
            const auto* a_line = line_topology_of(*a.route);
            const auto* b_line = line_topology_of(*b.route);

            if (const auto a_from = occurrence_key(a_line->from), b_from = occurrence_key(b_line->from); a_from != b_from)
                return a_from < b_from;

            if (const auto a_dep = a.ptr->departure->value(), b_dep = b.ptr->departure->value(); a_dep != b_dep)
                return a_dep < b_dep;

            if (const auto a_arr = a.ptr->arrival->value(), b_arr = b.ptr->arrival->value(); a_arr != b_arr)
                return a_arr < b_arr;

            if (const auto a_to = occurrence_key(a_line->to), b_to = occurrence_key(b_line->to); a_to != b_to)
                return a_to < b_to;

            if (a.ptr->trip != b.ptr->trip) {
                if (!a.ptr->trip) return true;
                if (!b.ptr->trip) return false;
                if (a.ptr->trip->get() != b.ptr->trip->get())
                    return a.ptr->trip->get() < b.ptr->trip->get();
            }

            if (a.ptr->from_index != b.ptr->from_index) {
                if (!a.ptr->from_index) return true;
                if (!b.ptr->from_index) return false;
                return a.ptr->from_index.value() < b.ptr->from_index.value();
            }

            if (a.ptr->to_index != b.ptr->to_index) {
                if (!a.ptr->to_index) return true;
                if (!b.ptr->to_index) return false;
                return a.ptr->to_index.value() < b.ptr->to_index.value();
            }

            if (a.route->id != b.route->id) return a.route->id.get() < b.route->id.get();
            return a.id.get() < b.id.get();
        }

        bool walk_ref_less(const ConnectionSegRef& a, const ConnectionSegRef& b) {
            if (const auto a_from = physical_from_key(*a.route), b_from = physical_from_key(*b.route); a_from != b_from)
                return a_from < b_from;

            if (const auto a_to = physical_to_key(*a.route), b_to = physical_to_key(*b.route); a_to != b_to)
                return a_to < b_to;

            if (a.route->id != b.route->id) return a.route->id.get() < b.route->id.get();
            return a.id.get() < b.id.get();
        }

        bool boarding_ref_less(const ConnectionSegRef& a, const ConnectionSegRef& b) {
            const auto a_from = line_topology_of(*a.route)->from.stop;
            const auto b_from = line_topology_of(*b.route)->from.stop;
            if (a_from != b_from)
                return a_from.get() < b_from.get();

            if (const auto a_dep = a.ptr->departure->value(), b_dep = b.ptr->departure->value(); a_dep != b_dep)
                return a_dep < b_dep;

            if (const auto a_arr = a.ptr->arrival->value(), b_arr = b.ptr->arrival->value(); a_arr != b_arr)
                return a_arr < b_arr;

            if (const auto a_occ = occurrence_key(line_topology_of(*a.route)->from)
                , b_occ = occurrence_key(line_topology_of(*b.route)->from)
                ; a_occ != b_occ
            ) return a_occ < b_occ;

            if (a.route->id != b.route->id)
                return a.route->id.get() < b.route->id.get();

            return a.id.get() < b.id.get();
        }

        std::vector<RouteSegRef> route_seg_refs_by_id(
            std::span<const RouteSegment> segments
        ) {
            std::vector<RouteSegRef> refs;
            refs.reserve(segments.size());
            std::transform(
                segments.begin(), segments.end()
                , std::back_inserter(refs)
                , [](const RouteSegment& s) {
                    return RouteSegRef{ s.id, &s };
                }
            );
            std::sort(refs.begin(), refs.end(), route_seg_ref_less);
            return refs;
        }

        PartitionedRouteRefs partition_route_refs(
            std::span<const RouteSegment> segments
        ) {
            PartitionedRouteRefs out;
            out.line.reserve(segments.size());
            out.walk.reserve(segments.size());

            for (const auto& segment : segments) {
                auto ref = RouteSegRef{ segment.id, &segment };
                if (is_line(segment)) {
                    out.line.push_back(ref);
                } else {
                    out.walk.push_back(ref);
                }
            }

            std::sort(out.line.begin(), out.line.end(), line_route_ref_less);
            std::sort(out.walk.begin(), out.walk.end(), walk_route_ref_less);
            return out;
        }

        const RouteSegment* find_route_segment(
            const std::vector<RouteSegRef>& refs
            , RouteSegmentId id
        ) {
            const auto it = std::lower_bound(
                refs.begin(), refs.end(), id.get(),
                [](const RouteSegRef& ref, std::int64_t value) {
                    return ref.id.get() < value;
                }
            );
            if (it == refs.end() || it->id != id)
                return nullptr;
            return it->ptr;
        }

        mathfp::Expected<ConnectionSegRef> make_connection_ref(
            const ConnectionSegment& segment
            , const std::vector<RouteSegRef>& route_refs
        ) {
            const auto* route = find_route_segment(route_refs, segment.route_segment);
            if (!route) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segment references unknown route segment")
                    .ctx("route_segment_id", segment.route_segment.get())
                );
            }

            if (!route_topology_matches_connection_mode(*route, segment)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segment mode does not match route topology")
                    .ctx("connection_segment_id", segment.id.get())
                    .ctx("route_segment_id", route->id.get())
                );
            }

            return ConnectionSegRef{ segment.id, &segment, route };
        }

        mathfp::Expected<PartitionedConnectionRefs> partition_connection_refs(
            std::span<const ConnectionSegment> segments
            , const std::vector<RouteSegRef>& route_refs
        ) {
            PartitionedConnectionRefs out;
            out.timed.reserve(segments.size());
            out.walk.reserve(segments.size());

            for (const auto& segment : segments) {
                MATHFP_TRY_LET(
                    ConnectionSegRef
                    , ref
                    , make_connection_ref(segment, route_refs)
                );
                if (is_timed_connection(segment)) {
                    out.timed.push_back(ref);
                } else {
                    out.walk.push_back(ref);
                }
            }

            std::sort(out.timed.begin(), out.timed.end(), timed_ref_less);
            std::sort(out.walk.begin(), out.walk.end(), walk_ref_less);
            return out;
        }

        template <class RefRange, class Key, class FromKeyFn>
        BucketIndex<Key> build_buckets(
            const RefRange& sorted_refs
            , FromKeyFn&& from_key
        ) {
            BucketIndex<Key> index;
            index.offsets.reserve(sorted_refs.size() + 1);
            for (std::size_t i = 0; i < sorted_refs.size(); ++i) {
                const auto key = from_key(sorted_refs[i]);
                if (index.buckets.empty() || key != index.buckets.back()) {
                    index.buckets.push_back(key);
                    index.offsets.push_back(i);
                }
            }
            index.offsets.push_back(sorted_refs.size());
            return index;
        }

        LineRouteIndexData fill_line_route_index(
            const std::vector<RouteSegRef>& line_refs
        ) {
            LineRouteIndexData data;
            data.order.reserve(line_refs.size());
            std::transform(
                line_refs.begin(), line_refs.end()
                , std::back_inserter(data.order)
                , [](const RouteSegRef& ref) { return ref.id; }
            );
            data.bucket_index = build_buckets<std::vector<RouteSegRef>, StopOccurrenceKey>(
                line_refs
                , [](const RouteSegRef& ref) { return occurrence_key(line_topology_of(*ref.ptr)->from); }
            );
            return data;
        }

        WalkRouteIndexData fill_walk_route_index(
            const std::vector<RouteSegRef>& walk_refs
        ) {
            WalkRouteIndexData data;
            data.order.reserve(walk_refs.size());
            std::transform(
                walk_refs.begin(), walk_refs.end()
                , std::back_inserter(data.order)
                , [](const RouteSegRef& ref) { return ref.id; }
            );
            data.bucket_index = build_buckets<std::vector<RouteSegRef>, EndpointKey>(
                walk_refs
                , [](const RouteSegRef& ref) { return physical_from_key(*ref.ptr); }
            );
            return data;
        }

        TimedConnectionIndexData fill_timed_connection_index(
            const std::vector<ConnectionSegRef>& timed_refs
        ) {
            TimedConnectionIndexData data;
            data.order.reserve(timed_refs.size());
            data.departures.reserve(timed_refs.size());
            std::transform(
                timed_refs.begin(), timed_refs.end()
                , std::back_inserter(data.order)
                , [](const ConnectionSegRef& ref) { return ref.id; }
            );
            std::transform(
                timed_refs.begin(), timed_refs.end()
                , std::back_inserter(data.departures)
                , [](const ConnectionSegRef& ref) { return *ref.ptr->departure; }
            );
            data.bucket_index = build_buckets<std::vector<ConnectionSegRef>, StopOccurrenceKey>(
                timed_refs
                , [](const ConnectionSegRef& ref) { return occurrence_key(line_topology_of(*ref.route)->from); }
            );
            return data;
        }

        BoardingConnectionIndexData fill_boarding_connection_index(
            std::vector<ConnectionSegRef> timed_refs
        ) {
            std::sort(timed_refs.begin(), timed_refs.end(), boarding_ref_less);

            BoardingConnectionIndexData data;
            data.order.reserve(timed_refs.size());
            data.departures.reserve(timed_refs.size());
            std::transform(
                timed_refs.begin(), timed_refs.end()
                , std::back_inserter(data.order)
                , [](const ConnectionSegRef& ref) { return ref.id; }
            );
            std::transform(
                timed_refs.begin(), timed_refs.end()
                , std::back_inserter(data.departures)
                , [](const ConnectionSegRef& ref) { return *ref.ptr->departure; }
            );
            data.bucket_index = build_buckets<std::vector<ConnectionSegRef>, StopId>(
                timed_refs
                , [](const ConnectionSegRef& ref) { return line_topology_of(*ref.route)->from.stop; }
            );
            return data;
        }

        WalkConnectionIndexData fill_walk_connection_index(
            const std::vector<ConnectionSegRef>& walk_refs
        ) {
            WalkConnectionIndexData data;
            data.order.reserve(walk_refs.size());
            std::transform(
                walk_refs.begin(), walk_refs.end()
                , std::back_inserter(data.order)
                , [](const ConnectionSegRef& ref) { return ref.id; }
            );
            data.bucket_index = build_buckets<std::vector<ConnectionSegRef>, EndpointKey>(
                walk_refs
                , [](const ConnectionSegRef& ref) { return physical_from_key(*ref.route); }
            );
            return data;
        }

        template <class Key>
        std::optional<std::size_t> find_bucket_impl(
            std::span<const Key> buckets
            , const Key& key
        ) {
            const auto it = std::lower_bound(buckets.begin(), buckets.end(), key);
            if (it == buckets.end() || *it != key)
                return std::nullopt;
            return static_cast<std::size_t>(std::distance(buckets.begin(), it));
        }

    }  // namespace

    mathfp::Expected<RouteSegmentIndex> build_route_segment_index(
        std::span<const RouteSegment> segments
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: route segment index");
        log(
            fmt::format(
                "route index input: route_segments = {:>8}"
                , segments.size()
            )
            , LogLevel::Info
        );

        auto refs = partition_route_refs(segments);
        auto line_index = fill_line_route_index(refs.line);
        auto walk_index = fill_walk_route_index(refs.walk);

        RouteSegmentIndex index{
            .line_order = std::move(line_index.order)
            , .line_buckets = std::move(line_index.bucket_index.buckets)
            , .line_offsets = std::move(line_index.bucket_index.offsets)
            , .walk_order = std::move(walk_index.order)
            , .walk_buckets = std::move(walk_index.bucket_index.buckets)
            , .walk_offsets = std::move(walk_index.bucket_index.offsets)
        };

        log(
            fmt::format(
                "route index: line_order = {:>8}  line_buckets = {:>6}\n"
                "             walk_order = {:>8}  walk_buckets = {:>6}"
                , index.line_order.size()
                , index.line_buckets.size()
                , index.walk_order.size()
                , index.walk_buckets.size()
            )
            , LogLevel::Info
        );
        both("preprocessing: route segment index done");

        return index;
    }

    mathfp::Expected<ConnectionSegmentIndex> build_connection_segment_index(
        std::span<const ConnectionSegment> segments
        , std::span<const RouteSegment> route_segments
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: connection segment index");
        log(
            fmt::format(
                "connection index input: connection_segments = {:>8}  route_segments = {:>8}"
                , segments.size()
                , route_segments.size()
            )
            , LogLevel::Info
        );

        const auto route_refs = route_seg_refs_by_id(route_segments);
        MATHFP_TRY_LET(
            PartitionedConnectionRefs
            , refs
            , partition_connection_refs(segments, route_refs)
        );

        auto timed_index = fill_timed_connection_index(refs.timed);
        auto boarding_index = fill_boarding_connection_index(refs.timed);
        auto walk_index = fill_walk_connection_index(refs.walk);

        ConnectionSegmentIndex index{
            .timed_order = std::move(timed_index.order)
            , .timed_departures = std::move(timed_index.departures)
            , .timed_buckets = std::move(timed_index.bucket_index.buckets)
            , .timed_offsets = std::move(timed_index.bucket_index.offsets)
            , .boarding_order = std::move(boarding_index.order)
            , .boarding_departures = std::move(boarding_index.departures)
            , .boarding_stop_buckets = std::move(boarding_index.bucket_index.buckets)
            , .boarding_offsets = std::move(boarding_index.bucket_index.offsets)
            , .walk_order = std::move(walk_index.order)
            , .walk_buckets = std::move(walk_index.bucket_index.buckets)
            , .walk_offsets = std::move(walk_index.bucket_index.offsets)
        };

        log(
            fmt::format(
                "connection index: timed_order = {:>8}  timed_buckets = {:>6}\n"
                "                  boarding_order = {:>8}  boarding_stop_buckets = {:>6}\n"
                "                  walk_order = {:>8}  walk_buckets  = {:>6}"
                , index.timed_order.size()
                , index.timed_buckets.size()
                , index.boarding_order.size()
                , index.boarding_stop_buckets.size()
                , index.walk_order.size()
                , index.walk_buckets.size()
            ),
            LogLevel::Info
        );
        both("preprocessing: connection segment index done");

        return index;
    }

    std::optional<std::size_t> find_bucket(
        std::span<const EndpointKey> buckets
        , const EndpointKey& key
    ) {
        return find_bucket_impl(buckets, key);
    }

    std::optional<std::size_t> find_bucket(
        std::span<const StopOccurrenceKey> buckets
        , const StopOccurrenceKey& key
    ) {
        return find_bucket_impl(buckets, key);
    }

    std::optional<std::size_t> find_bucket(
        std::span<const StopId> buckets
        , StopId key
    ) {
        return find_bucket_impl(buckets, key);
    }

    SegmentLookup lookup_from(
        const RouteSegmentIndex& route_index
        , const ConnectionSegmentIndex& connection_index
        , EndpointKey physical_from
    ) {
        SegmentLookup out;

        if (auto idx = find_bucket(route_index.walk_buckets, physical_from)) {
            const auto i = *idx;
            const auto start = route_index.walk_offsets[i];
            const auto end = route_index.walk_offsets[i + 1];
            out.walk_route = std::span<const RouteSegmentId>(
                route_index.walk_order.data() + start
                , end - start
            );
        }

        if (auto idx = find_bucket(connection_index.walk_buckets, physical_from)) {
            const auto i = *idx;
            const auto start = connection_index.walk_offsets[i];
            const auto end = connection_index.walk_offsets[i + 1];
            out.walk_connections = std::span<const ConnectionSegmentId>(
                connection_index.walk_order.data() + start
                , end - start
            );
        }

        return out;
    }

    SegmentLookup lookup_from(
        const RouteSegmentIndex& route_index
        , const ConnectionSegmentIndex& connection_index
        , StopOccurrenceKey timed_from
        , EndpointKey physical_from
    ) {
        auto out = lookup_from(route_index, connection_index, physical_from);

        if (auto idx = find_bucket(route_index.line_buckets, timed_from)) {
            const auto i = *idx;
            const auto start = route_index.line_offsets[i];
            const auto end = route_index.line_offsets[i + 1];
            out.line_route = std::span<const RouteSegmentId>(
                route_index.line_order.data() + start
                , end - start
            );
        }

        if (auto idx = find_bucket(connection_index.timed_buckets, timed_from)) {
            const auto i = *idx;
            const auto start = connection_index.timed_offsets[i];
            const auto end = connection_index.timed_offsets[i + 1];
            out.timed_connections = std::span<const ConnectionSegmentId>(
                connection_index.timed_order.data() + start
                , end - start
            );
        }

        return out;
    }

    std::optional<ConnectionSegmentId> next_connection_from(
        const ConnectionSegmentIndex& connection_index
        , StopOccurrenceKey from
        , Time time
    ) {
        const auto bucket = find_bucket(connection_index.timed_buckets, from);
        if (!bucket)
            return std::nullopt;

        const auto i = *bucket;
        const auto start = connection_index.timed_offsets[i];
        const auto end = connection_index.timed_offsets[i + 1];
        if (start >= end)
            return std::nullopt;

        const auto begin = connection_index.timed_departures.begin() + static_cast<std::ptrdiff_t>(start);
        const auto finish = connection_index.timed_departures.begin() + static_cast<std::ptrdiff_t>(end);

        const auto it = std::lower_bound(begin, finish, time, [](const Time& a, const Time& b) {
            return a.value() < b.value();
        });

        if (it == finish)
            return std::nullopt;

        const auto idx = static_cast<std::size_t>(std::distance(connection_index.timed_departures.begin(), it));
        return connection_index.timed_order[idx];
    }

}  // namespace timetable::domain::preprocessing
