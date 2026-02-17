#include "timetable/domain/preprocessing/segments_index.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include <mathfp/core/error.hpp>

#include <fmt/format.h>

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

        bool route_seg_ref_less(const RouteSegRef& a, const RouteSegRef& b) {
            return a.id.get() < b.id.get();
        }

        bool route_seg_order_less(const RouteSegRef& a, const RouteSegRef& b) {
            return route_segment_less(*a.ptr, *b.ptr);
        }

        bool timed_ref_less(const ConnectionSegRef& a, const ConnectionSegRef& b) {
            if (const auto a_from = to_endpoint_key(a.route->from), b_from = to_endpoint_key(b.route->from); a_from != b_from)
                return a_from < b_from;

            if (const auto a_dep = a.ptr->departure->value(), b_dep = b.ptr->departure->value(); a_dep != b_dep)
                return a_dep < b_dep;

            if (const auto a_arr = a.ptr->arrival->value(), b_arr = b.ptr->arrival->value(); a_arr != b_arr)
                return a_arr < b_arr;

            if (const auto a_to = to_endpoint_key(a.route->to), b_to = to_endpoint_key(b.route->to); a_to != b_to)
                return a_to < b_to;

            if (a.route->id != b.route->id) return a.route->id.get() < b.route->id.get();
            return a.id.get() < b.id.get();
        }

        bool walk_ref_less(const ConnectionSegRef& a, const ConnectionSegRef& b) {
            if (const auto a_from = to_endpoint_key(a.route->from), b_from = to_endpoint_key(b.route->from); a_from != b_from)
                return a_from < b_from;

            if (const auto a_to = to_endpoint_key(a.route->to), b_to = to_endpoint_key(b.route->to); a_to != b_to)
                return a_to < b_to;

            if (a.route->id != b.route->id) return a.route->id.get() < b.route->id.get();
            return a.id.get() < b.id.get();
        }

        auto route_seg_refs(std::span<const RouteSegment> segments) {
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

        void build_buckets(
            std::vector<EndpointKey>& buckets
            , std::vector<std::size_t>& offsets
            , const auto& sorted_refs
            , auto&& from_key
        ) {
            buckets.clear();
            offsets.clear();
            offsets.reserve(sorted_refs.size() + 1);
            offsets.push_back(0);

            EndpointKey last{};
            bool has_last = false;
            for (std::size_t i = 0; i < sorted_refs.size(); ++i) {
                const auto key = from_key(sorted_refs[i]);
                if (!has_last || key != last) {
                    buckets.push_back(key);
                    offsets.push_back(i);
                    last = key;
                    has_last = true;
                }
            }
            offsets.push_back(sorted_refs.size());
            if (!buckets.empty())
                offsets.erase(offsets.begin());
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

        auto refs = route_seg_refs(segments);
        std::sort(refs.begin(), refs.end(), route_seg_order_less);

        RouteSegmentIndex index;
        index.order.reserve(refs.size());
        std::transform(
            refs.begin(), refs.end(),
            std::back_inserter(index.order),
            [](const RouteSegRef& ref) { return ref.id; }
        );

        build_buckets(
            index.buckets
            , index.offsets
            , refs
            , [](const RouteSegRef& ref) { return to_endpoint_key(ref.ptr->from); }
        );

        log(
            fmt::format(
                "route index: order = {:>8}  buckets = {:>6}"
                , index.order.size()
                , index.buckets.size()
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

        const auto route_refs = route_seg_refs(route_segments);

        std::vector<ConnectionSegRef> timed_refs;
        std::vector<ConnectionSegRef> walk_refs;
        timed_refs.reserve(segments.size());
        walk_refs.reserve(segments.size());

        for (const auto& s : segments) {
            const auto* route = find_route_segment(route_refs, s.route_segment);
            if (!route)
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segment references unknown route segment")
                    .ctx("route_segment_id", s.route_segment.get())
                );

            const auto line = is_line(route->carrier);
            const auto has_times = s.departure.has_value();
            if (line && !has_times)
                return mathfp::unexpected(
                    mathfp::invalid_arg("timed connection segment must have times")
                    .ctx("connection_segment_id", s.id.get())
                );
            if (!line && has_times)
                return mathfp::unexpected(
                    mathfp::invalid_arg("walk connection segment must not have times")
                    .ctx("connection_segment_id", s.id.get())
                );

            ConnectionSegRef ref{ s.id, &s, route };
            if (line)
                timed_refs.push_back(ref);
            else
                walk_refs.push_back(ref);
        }

        std::sort(timed_refs.begin(), timed_refs.end(), timed_ref_less);

        std::sort(walk_refs.begin(), walk_refs.end(), walk_ref_less);

        ConnectionSegmentIndex index;
        index.timed_order.reserve(timed_refs.size());
        index.timed_departures.reserve(timed_refs.size());
        std::transform(
            timed_refs.begin(), timed_refs.end(),
            std::back_inserter(index.timed_order),
            [](const ConnectionSegRef& ref) { return ref.id; }
        );
        std::transform(
            timed_refs.begin(), timed_refs.end(),
            std::back_inserter(index.timed_departures),
            [](const ConnectionSegRef& ref) { return *ref.ptr->departure; }
        );
        index.walk_order.reserve(walk_refs.size());
        std::transform(
            walk_refs.begin(), walk_refs.end(),
            std::back_inserter(index.walk_order),
            [](const ConnectionSegRef& ref) { return ref.id; }
        );

        build_buckets(
            index.timed_buckets
            , index.timed_offsets
            , timed_refs
            , [](const ConnectionSegRef& ref) { return to_endpoint_key(ref.route->from); }
        );

        build_buckets(
            index.walk_buckets
            , index.walk_offsets
            , walk_refs
            , [](const ConnectionSegRef& ref) { return to_endpoint_key(ref.route->from); }
        );

        log(
            fmt::format(
                "connection index: timed_order = {:>8}  walk_order = {:>8}\n"
                "                 timed_buckets = {:>6}  walk_buckets = {:>6}"
                , index.timed_order.size()
                , index.walk_order.size()
                , index.timed_buckets.size()
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
        const auto it = std::lower_bound(buckets.begin(), buckets.end(), key);
        if (it == buckets.end() || *it != key)
            return std::nullopt;
        return static_cast<std::size_t>(std::distance(buckets.begin(), it));
    }

    SegmentLookup lookup_from(
        const RouteSegmentIndex& route_index
        , const ConnectionSegmentIndex& connection_index
        , EndpointKey from
    ) {
        SegmentLookup out;

        if (auto idx = find_bucket(route_index.buckets, from)) {
            const auto i = *idx;
            const auto start = route_index.offsets[i];
            const auto end = route_index.offsets[i + 1];
            out.route = std::span<const RouteSegmentId>(
                route_index.order.data() + start
                , end - start
            );
        }

        if (auto idx = find_bucket(connection_index.timed_buckets, from)) {
            const auto i = *idx;
            const auto start = connection_index.timed_offsets[i];
            const auto end = connection_index.timed_offsets[i + 1];
            out.timed_connections = std::span<const ConnectionSegmentId>(
                connection_index.timed_order.data() + start
                , end - start
            );
        }

        if (auto idx = find_bucket(connection_index.walk_buckets, from)) {
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

    std::optional<ConnectionSegmentId> next_connection_from(
        const ConnectionSegmentIndex& connection_index
        , EndpointKey from
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

        const auto begin = connection_index.timed_departures.begin() + start;
        const auto finish = connection_index.timed_departures.begin() + end;

        const auto it = std::lower_bound(begin, finish, time, [](const Time& a, const Time& b) {
            return a.value() < b.value();
        });

        if (it == finish)
            return std::nullopt;

        const auto idx = static_cast<std::size_t>(std::distance(connection_index.timed_departures.begin(), it));
        return connection_index.timed_order[idx];
    }

}  // namespace timetable::domain::preprocessing
