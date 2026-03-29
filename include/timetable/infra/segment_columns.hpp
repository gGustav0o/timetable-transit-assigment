#pragma once

#include <cstdint>
#include <vector>

namespace timetable::infra {

    /**
     * @brief Format-agnostic column bundle for pre-segmented transit input.
     *
     * Both single-file TXT and pair CSV parsers map their raw source into this DTO
     * before the common builder constructs AssignmentInput.
     */
    struct SegmentColumns final {
        std::vector<std::int64_t> from_zone_id{};
        std::vector<std::int64_t> from_stop_id{};
        std::vector<std::int64_t> to_zone_id{};
        std::vector<std::int64_t> to_stop_id{};
        std::vector<std::int64_t> profile_id{};
        std::vector<std::int64_t> trip_id{};
        std::vector<std::int64_t> from_index{};
        std::vector<std::int64_t> to_index{};
        std::vector<double>       length_km{};
        std::vector<double>       time_sec{};
        std::vector<double>       dep_sec{};
        std::vector<double>       arr_sec{};
        std::vector<double>       fare{};
        std::vector<std::int64_t> zone_ids{};
    };

}  // namespace timetable::infra
