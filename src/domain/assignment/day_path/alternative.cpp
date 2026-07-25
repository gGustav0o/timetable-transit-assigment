#include "timetable/domain/assignment/day_path/alternative.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_day_path_alternative(
          const DayPathAlternative& alternative
        , std::size_t               alternative_index
    ) {
        const auto& identity = day_path_signature_of(alternative);
        if (alternative.support.split_support.supports.empty()) {
            return mathfp::unexpected(
                mathfp::internal_error("day-path alternative has empty timed support")
                    .ctx("alternative_index", static_cast<std::int64_t>(alternative_index))
                    .ctx("origin", identity.origin.get())
                    .ctx("destination", identity.destination.get())
            );
        }

        const auto representative_signature =
            day_path_signature_of(alternative.support.representative);
        if (!(representative_signature == identity)) {
            return mathfp::unexpected(
                mathfp::internal_error("day-path representative disagrees with structural identity")
                    .ctx("alternative_index", static_cast<std::int64_t>(alternative_index))
                    .ctx("origin", identity.origin.get())
                    .ctx("destination", identity.destination.get())
            );
        }

        for (std::size_t i = 0; i < alternative.support.split_support.supports.size(); ++i) {
            if (!(alternative.support.split_support.supports[i].signature == identity)) {
                return mathfp::unexpected(
                    mathfp::internal_error("day-path timed support disagrees with structural identity")
                        .ctx("alternative_index", static_cast<std::int64_t>(alternative_index))
                        .ctx("support_index", static_cast<std::int64_t>(i))
                        .ctx("origin", identity.origin.get())
                        .ctx("destination", identity.destination.get())
                );
            }
        }

        return mathfp::kUnit;
    }

    DayPathAlternative make_day_path_alternative_from_metrics(
          SearchConnection          connection
        , DayPathSignature          signature
        , CompleteConnectionMetrics complete_metrics
        , ConnectionMetrics         connection_metrics
    ) {
        std::vector<DayPathSupportDescriptor> supports;
        supports.push_back(
            make_day_path_support_descriptor(
                  connection
                , signature
                , complete_metrics
                , connection_metrics
            )
        );
        return DayPathAlternative{
              .identity = DayPathIdentity{
                  .signature = std::move(signature)
              }
            , .support = DayPathTimedSupport{
                  .representative                    = std::move(connection)
                , .representative_metrics            = complete_metrics
                , .representative_connection_metrics = connection_metrics
                , .split_support                     = DayPathSplitSupport{
                      .supports = std::move(supports)
                  }
              }
        };
    }

}  // namespace timetable::domain::assignment
