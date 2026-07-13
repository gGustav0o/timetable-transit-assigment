#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params/transfer_limits.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    namespace params_detail {

        inline mathfp::Expected<mathfp::Unit> ensure_max_transfers_nonnegative(
            TransferCount max_transfers
        ) {
            if (max_transfers.get() < 0) {
                const char* message = "max_transfers must be non-negative";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message)
                        .ctx("max_transfers", max_transfers.get())
                );
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_transfer_waits_ordered(
              Time min_transfer_wait
            , Time max_transfer_wait
        ) {
            if (min_transfer_wait.value() > max_transfer_wait.value()) {
                const char* message = "min_transfer_wait must be <= max_transfer_wait";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message)
                        .ctx("min_transfer_wait", min_transfer_wait.value())
                        .ctx("max_transfer_wait", max_transfer_wait.value())
                );
            }
            return mathfp::kUnit;
        }

    }  // namespace params_detail

    inline mathfp::Expected<TransferLimits> make_transfer_limits(
          TransferCount max_transfers
        , Time          min_transfer_wait
        , Time          max_transfer_wait
        , bool          allow_start_wait
        , bool          allow_end_wait
    ) {
        MATHFP_TRY(params_detail::ensure_max_transfers_nonnegative(max_transfers));
        MATHFP_TRY(validation::ensure_nonneg(min_transfer_wait, "min_transfer_wait"));
        MATHFP_TRY(validation::ensure_nonneg(max_transfer_wait, "max_transfer_wait"));
        MATHFP_TRY(params_detail::ensure_transfer_waits_ordered(
            min_transfer_wait, max_transfer_wait
        ));

        return TransferLimits{
              .max_transfers     = max_transfers
            , .min_transfer_wait = min_transfer_wait
            , .max_transfer_wait = max_transfer_wait
            , .allow_start_wait  = allow_start_wait
            , .allow_end_wait    = allow_end_wait
        };
    }

}  // namespace timetable::domain
