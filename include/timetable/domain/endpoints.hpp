#pragma once

#include <compare>
#include <cstdint>

#include <boost/container_hash/hash.hpp>

#include "timetable/domain/model.hpp"

namespace timetable::domain {

    /**
     * @brief Key in the physical endpoint space.
     *
     * This identifies stops/zones as transfer-access nodes. It intentionally
     * does not distinguish repeated occurrences of the same stop on a loop line.
     */
    enum class EndpointKind : std::uint8_t {
        Stop
        , Zone
    };

    struct EndpointKey final {
        EndpointKind kind{};
        std::int64_t id{};

        auto operator<=>(const EndpointKey&) const = default;
    };

    /**
     * @brief Key in the timetable occurrence space.
     *
     * Unlike EndpointKey, this distinguishes repeated appearances of the same
     * physical stop by route position.
     */
    struct StopOccurrenceKey final {
        StopId        stop{};
        RoutePosition position{};

        auto operator<=>(const StopOccurrenceKey&) const = default;
    };

    [[nodiscard]] constexpr EndpointKey to_endpoint_key(const WalkEndpoint& endpoint) noexcept {
        if (std::holds_alternative<StopId>(endpoint))
            return EndpointKey{ EndpointKind::Stop, std::get<StopId>(endpoint).get() };
        return EndpointKey{ EndpointKind::Zone, std::get<ZoneId>(endpoint).get() };
    }

    [[nodiscard]] constexpr EndpointKey endpoint_key(StopId id) noexcept {
        return EndpointKey{ EndpointKind::Stop, id.get() };
    }

    [[nodiscard]] constexpr EndpointKey endpoint_key(ZoneId id) noexcept {
        return EndpointKey{ EndpointKind::Zone, id.get() };
    }

    [[nodiscard]] constexpr StopOccurrenceKey occurrence_key(
        StopOccurrence occurrence
    ) noexcept {
        return StopOccurrenceKey{
            .stop = occurrence.stop
            , .position = occurrence.position
        };
    }

}  // namespace timetable::domain

namespace std {
    template <>
    struct hash<timetable::domain::EndpointKey> {
        size_t operator()(const timetable::domain::EndpointKey& k) const noexcept {
            std::size_t seed = 0;
            boost::hash_combine(seed, static_cast<std::uint8_t>(k.kind));
            boost::hash_combine(seed, k.id);
            return seed;
        }
    };

    template <>
    struct hash<timetable::domain::StopOccurrenceKey> {
        size_t operator()(const timetable::domain::StopOccurrenceKey& k) const noexcept {
            std::size_t seed = 0;
            boost::hash_combine(seed, k.stop.get());
            boost::hash_combine(seed, k.position.get());
            return seed;
        }
    };
}
