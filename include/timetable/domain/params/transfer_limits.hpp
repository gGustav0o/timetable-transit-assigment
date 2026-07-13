#pragma once

#include <cstdint>
#include <optional>
#include <source_location>
#include <utility>

#include <mathfp/core/checked_arithmetic.hpp>
#include <mathfp/core/expected.hpp>

#include "timetable/domain/id.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    struct TransferCountTag {};

    using TransferCount = OrderedDomainValue<TransferCountTag, std::int32_t>;

    [[nodiscard]] inline mathfp::Expected<TransferCount> next_transfer_count(
          TransferCount         value
        , std::source_location where = std::source_location::current()
    ) {
        auto raw = mathfp::checked_add(value.get(), std::int32_t{1}, where);
        if (!raw) {
            return mathfp::unexpected(std::move(raw.error()));
        }
        return TransferCount{ *raw };
    }

    [[nodiscard]] inline std::optional<TransferCount> bounded_next_transfer_count(
          TransferCount         value
        , TransferCount         upper_bound
        , std::source_location where = std::source_location::current()
    ) {
        if (!(value < upper_bound)) {
            return std::nullopt;
        }

        auto raw = mathfp::checked_add(value.get(), std::int32_t{1}, where);
        if (!raw) {
            return std::nullopt;
        }
        return TransferCount{ *raw };
    }

    /**
     * @brief Hard constraints for transfer feasibility during search.
     */
    struct TransferLimits final {
        TransferCount max_transfers{ TransferCount{0} };
        Time          min_transfer_wait{};
        Time          max_transfer_wait{};
        bool          allow_start_wait{};
        bool          allow_end_wait{};
    };

}  // namespace timetable::domain
