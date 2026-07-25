#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/day_path/finalization.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/params/make/tolerances.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] ConnectionLeg access_walk_leg(
        ZoneId origin
    ) {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::AccessWalk
            , .connection_segment = ConnectionSegmentId{ 10 }
            , .route_segment      = RouteSegmentId{ 20 }
            , .physical_from      = endpoint_key(origin)
            , .physical_to        = endpoint_key(StopId{ 100 })
            , .start_time         = Time{ 0.0 }
            , .end_time           = Time{ 5.0 }
            , .length             = Length{ 1.0 }
            , .fare               = 0.0
        };
    }

    [[nodiscard]] ConnectionLeg ride_leg() {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::Ride
            , .connection_segment = ConnectionSegmentId{ 11 }
            , .route_segment      = RouteSegmentId{ 21 }
            , .physical_from      = endpoint_key(StopId{ 100 })
            , .physical_to        = endpoint_key(StopId{ 200 })
            , .occurrence_from    = StopOccurrenceKey{
                  .stop     = StopId{ 100 }
                , .position = RoutePosition{ 2 }
              }
            , .occurrence_to      = StopOccurrenceKey{
                  .stop     = StopId{ 200 }
                , .position = RoutePosition{ 4 }
              }
            , .line               = LineId{ 30 }
            , .route              = RouteId{ 40 }
            , .trip               = TripId{ 50 }
            , .start_time         = Time{ 5.0 }
            , .end_time           = Time{ 17.0 }
            , .length             = Length{ 3.0 }
            , .fare               = 2.0
        };
    }

    [[nodiscard]] ConnectionLeg egress_walk_leg(
        ZoneId destination
    ) {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::EgressWalk
            , .connection_segment = ConnectionSegmentId{ 12 }
            , .route_segment      = RouteSegmentId{ 22 }
            , .physical_from      = endpoint_key(StopId{ 200 })
            , .physical_to        = endpoint_key(destination)
            , .start_time         = Time{ 17.0 }
            , .end_time           = Time{ 20.0 }
            , .length             = Length{ 1.0 }
            , .fare               = 0.0
        };
    }

    [[nodiscard]] SearchConnection search_connection(
          ZoneId origin
        , ZoneId destination
    ) {
        auto connection = make_search_connection(
              origin
            , destination
            , ConnectionTrace{
                  .legs = {
                      access_walk_leg(origin)
                    , ride_leg()
                    , egress_walk_leg(destination)
                  }
              }
        );
        if (!connection) {
            ADD_FAILURE() << connection.error().to_string();
            return make_search_connection(
                  ZoneId{ 1 }
                , ZoneId{ 2 }
                , ConnectionTrace{
                      .legs = {
                          access_walk_leg(ZoneId{ 1 })
                        , ride_leg()
                        , egress_walk_leg(ZoneId{ 2 })
                      }
                  }
            ).value();
        }
        return *connection;
    }

    [[nodiscard]] CompleteConnectionMetrics metrics(
          double departure
        , double arrival
        , double impedance
        , int    transfers
    ) {
        return CompleteConnectionMetrics{
              .departure    = Time{ departure }
            , .arrival      = Time{ arrival }
            , .journey_time = Time{ arrival - departure }
            , .transfers    = TransferCount{ transfers }
            , .impedance    = impedance
        };
    }

    [[nodiscard]] DayPathAlternative alternative_with_representative_metrics(
          ZoneId                    origin
        , ZoneId                    destination
        , CompleteConnectionMetrics representative_metrics
        , std::size_t               support_count
    ) {
        auto representative = search_connection(origin, destination);
        const auto signature = day_path_signature_of(representative);
        std::vector<DayPathSupportDescriptor> supports;
        supports.reserve(support_count);
        for (std::size_t i = 0; i < support_count; ++i) {
            supports.push_back(DayPathSupportDescriptor{
                  .signature        = signature
                , .complete_metrics = representative_metrics
            });
        }
        return DayPathAlternative{
              .identity = DayPathIdentity{ .signature = signature }
            , .support = DayPathTimedSupport{
                  .representative         = std::move(representative)
                , .representative_metrics = representative_metrics
                , .split_support          = DayPathSplitSupport{
                      .supports = std::move(supports)
                  }
              }
        };
    }

    void insert_alternative(
          DayPathRetention&   retention
        , DayPathAlternative  alternative
    ) {
        const auto key = alternative.identity.signature;
        retention.alternatives_by_signature.emplace(key, std::move(alternative));
    }

    [[nodiscard]] ChoiceTolerances exact_choice_tolerances() {
        auto tolerances = make_choice_tolerances(
              Dimless{ 1.0 }
            , Dimless{ 0.0 }
            , Dimless{ 1.0 }
            , Time{ 0.0 }
            , Dimless{ 1.0 }
            , Dimless{ 0.0 }
        );
        if (!tolerances) {
            ADD_FAILURE() << tolerances.error().to_string();
            return ChoiceTolerances{};
        }
        return *tolerances;
    }

}  // namespace

TEST(DayPathFinalization, MetricsOfAlternativeUsesRepresentativeAndTimedSupportCount) {
    const auto alternative = alternative_with_representative_metrics(
          ZoneId{ 1 }
        , ZoneId{ 2 }
        , metrics(0.0, 20.0, 8.0, 1)
        , 3u
    );

    const auto path_metrics = day_path_metrics_of(alternative);

    EXPECT_DOUBLE_EQ(path_metrics.complete.impedance, 8.0);
    EXPECT_EQ(path_metrics.complete.transfers, TransferCount{ 1 });
    EXPECT_EQ(path_metrics.timed_connection_count, 3u);
}

TEST(DayPathFinalization, AlternativesAreMaterializedInSignatureOrder) {
    auto first = alternative_with_representative_metrics(
          ZoneId{ 1 }
        , ZoneId{ 2 }
        , metrics(0.0, 20.0, 8.0, 0)
        , 1u
    );
    auto second = alternative_with_representative_metrics(
          ZoneId{ 1 }
        , ZoneId{ 3 }
        , metrics(0.0, 25.0, 9.0, 1)
        , 1u
    );
    const auto first_signature = first.identity.signature;
    const auto second_signature = second.identity.signature;
    DayPathRetention retention;
    insert_alternative(retention, std::move(second));
    insert_alternative(retention, std::move(first));

    const auto alternatives = finalize_day_path_alternatives(std::move(retention));

    ASSERT_EQ(alternatives.size(), 2u);
    EXPECT_EQ(alternatives[0].identity.signature, first_signature);
    EXPECT_EQ(alternatives[1].identity.signature, second_signature);
}

TEST(DayPathFinalization, RepresentativesFollowFinalizedAlternativeOrder) {
    auto first = alternative_with_representative_metrics(
          ZoneId{ 1 }
        , ZoneId{ 2 }
        , metrics(0.0, 20.0, 8.0, 0)
        , 1u
    );
    auto second = alternative_with_representative_metrics(
          ZoneId{ 1 }
        , ZoneId{ 3 }
        , metrics(0.0, 25.0, 9.0, 1)
        , 1u
    );
    const auto first_signature = first.identity.signature;
    const auto second_signature = second.identity.signature;
    DayPathRetention retention;
    insert_alternative(retention, std::move(second));
    insert_alternative(retention, std::move(first));

    const auto representatives = finalize_day_path_representatives(std::move(retention));

    ASSERT_EQ(representatives.size(), 2u);
    EXPECT_EQ(day_path_signature_of(representatives[0]), first_signature);
    EXPECT_EQ(day_path_signature_of(representatives[1]), second_signature);
}

TEST(DayPathFinalization, SummaryUsesRepresentativeMetricMinima) {
    DayPathRetention retention;
    insert_alternative(
          retention
        , alternative_with_representative_metrics(
              ZoneId{ 1 }
            , ZoneId{ 2 }
            , metrics(0.0, 20.0, 8.0, 1)
            , 1u
          )
    );
    insert_alternative(
          retention
        , alternative_with_representative_metrics(
              ZoneId{ 1 }
            , ZoneId{ 3 }
            , metrics(0.0, 15.0, 12.0, 0)
            , 1u
          )
    );

    const auto summary = summarize_day_path_metrics(retention);

    EXPECT_FALSE(summary.empty);
    EXPECT_DOUBLE_EQ(summary.min_impedance, 8.0);
    EXPECT_DOUBLE_EQ(summary.min_journey_time, 15.0);
    EXPECT_DOUBLE_EQ(summary.min_transfers, 0.0);
}

TEST(DayPathFinalization, ExactOnlySkipsChoiceToleranceFiltering) {
    DayPathRetention retention;
    insert_alternative(
          retention
        , alternative_with_representative_metrics(
              ZoneId{ 1 }
            , ZoneId{ 2 }
            , metrics(0.0, 20.0, 5.0, 0)
            , 1u
          )
    );
    insert_alternative(
          retention
        , alternative_with_representative_metrics(
              ZoneId{ 1 }
            , ZoneId{ 3 }
            , metrics(0.0, 25.0, 6.0, 1)
            , 1u
          )
    );

    const auto alternatives = finalize_day_path_alternatives(
          std::move(retention)
        , exact_choice_tolerances()
        , ChoiceRolloutStage::ExactOnly
    );

    EXPECT_EQ(alternatives.size(), 2u);
}

TEST(DayPathFinalization, ExactAndApproximateAppliesChoiceToleranceFiltering) {
    const auto retained = alternative_with_representative_metrics(
          ZoneId{ 1 }
        , ZoneId{ 2 }
        , metrics(0.0, 20.0, 5.0, 0)
        , 1u
    );
    const auto retained_signature = retained.identity.signature;
    DayPathRetention retention;
    insert_alternative(retention, retained);
    insert_alternative(
          retention
        , alternative_with_representative_metrics(
              ZoneId{ 1 }
            , ZoneId{ 3 }
            , metrics(0.0, 25.0, 6.0, 1)
            , 1u
          )
    );

    const auto alternatives = finalize_day_path_alternatives(
          std::move(retention)
        , exact_choice_tolerances()
        , ChoiceRolloutStage::ExactAndApproximate
    );

    ASSERT_EQ(alternatives.size(), 1u);
    EXPECT_EQ(alternatives[0].identity.signature, retained_signature);
}

}  // namespace timetable::domain::assignment
