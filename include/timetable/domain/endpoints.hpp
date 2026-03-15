#pragma once

#include <compare>
#include <cstdint>

#include <boost/container_hash/hash.hpp>

#include "timetable/domain/model.hpp"

namespace timetable::domain {

    enum class EndpointKind : std::uint8_t {
        Stop
        , Zone
    };

    struct EndpointKey final {
        EndpointKind kind{};
        std::int64_t id{};

        auto operator<=>(const EndpointKey&) const = default;
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
}
