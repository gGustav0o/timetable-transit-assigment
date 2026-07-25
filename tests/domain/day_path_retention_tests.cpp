#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/day_path/retention.hpp"
#include "timetable/domain/endpoints.hpp"

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

    [[nodiscard]] DayPathSignature signature(
          ZoneId origin
        , ZoneId destination
    ) {
        return day_path_signature_of(search_connection(origin, destination));
    }

    [[nodiscard]] DayPathSupportDescriptor support_descriptor(
          DayPathSignature          path_signature
        , CompleteConnectionMetrics complete_metrics
    ) {
        return DayPathSupportDescriptor{
              .signature        = std::move(path_signature)
            , .complete_metrics = complete_metrics
        };
    }

    [[nodiscard]] DayPathAlternative alternative_with_support_metrics(
          DayPathSignature                       path_signature
        , std::vector<CompleteConnectionMetrics> support_metrics
    ) {
        auto connection = search_connection(
              path_signature.origin
            , path_signature.destination
        );
        std::vector<DayPathSupportDescriptor> supports;
        supports.reserve(support_metrics.size());
        for (const auto metric : support_metrics) {
            supports.push_back(support_descriptor(path_signature, metric));
        }

        return DayPathAlternative{
              .identity = DayPathIdentity{ .signature = path_signature }
            , .support = DayPathTimedSupport{
                  .representative         = std::move(connection)
                , .representative_metrics = metrics(0.0, 20.0, 10.0, 0)
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

}  // namespace

TEST(DayPathRetention, RepresentativeOrderingPrefersDominatingMetrics) {
    const auto dominant = metrics(10.0, 20.0, 5.0, 0);
    const auto dominated = metrics(9.0, 21.0, 6.0, 1);

    EXPECT_TRUE(better_day_path_representative(dominant, dominated));
    EXPECT_FALSE(better_day_path_representative(dominated, dominant));
}

TEST(DayPathRetention, RepresentativeOrderingUsesImpedanceForNondominatedMetrics) {
    const auto lower_impedance = metrics(0.0, 30.0, 5.0, 1);
    const auto higher_impedance = metrics(0.0, 20.0, 6.0, 0);

    EXPECT_TRUE(better_day_path_representative(lower_impedance, higher_impedance));
    EXPECT_FALSE(better_day_path_representative(higher_impedance, lower_impedance));
}

TEST(DayPathRetention, UnboundedSupportRetentionAppendsTimedSupports) {
    const auto path_signature = signature(ZoneId{ 1 }, ZoneId{ 2 });
    DayPathSplitSupport support;

    const auto first = retain_day_path_support(
          support
        , support_descriptor(path_signature, metrics(0.0, 20.0, 10.0, 0))
        , DayPathRetentionConfig{}
    );
    ASSERT_TRUE(first);

    const auto second = retain_day_path_support(
          support
        , support_descriptor(path_signature, metrics(1.0, 21.0, 11.0, 0))
        , DayPathRetentionConfig{}
    );
    ASSERT_TRUE(second);
    EXPECT_EQ(support.supports.size(), 2u);
}

TEST(DayPathRetention, BoundedDropWorstSupportRetentionKeepsBetterSupport) {
    const auto path_signature = signature(ZoneId{ 1 }, ZoneId{ 2 });
    auto support = DayPathSplitSupport{
        .supports = {
            support_descriptor(path_signature, metrics(0.0, 20.0, 20.0, 0))
        }
    };
    const auto config = DayPathRetentionConfig{
          .limit_policy = DayPathRetentionLimitPolicy::BoundedDropWorst
        , .max_supports_per_path = std::size_t{ 1 }
    };

    const auto retained = retain_day_path_support(
          support
        , support_descriptor(path_signature, metrics(0.0, 25.0, 5.0, 1))
        , config
    );

    ASSERT_TRUE(retained);
    ASSERT_EQ(support.supports.size(), 1u);
    EXPECT_DOUBLE_EQ(support.supports[0].complete_metrics.impedance, 5.0);
}

TEST(DayPathRetention, FailOnSaturationRejectsAdditionalTimedSupport) {
    const auto path_signature = signature(ZoneId{ 1 }, ZoneId{ 2 });
    auto support = DayPathSplitSupport{
        .supports = {
            support_descriptor(path_signature, metrics(0.0, 20.0, 10.0, 0))
        }
    };
    const auto config = DayPathRetentionConfig{
          .limit_policy = DayPathRetentionLimitPolicy::FailOnSaturation
        , .max_supports_per_path = std::size_t{ 1 }
    };

    const auto retained = retain_day_path_support(
          support
        , support_descriptor(path_signature, metrics(0.0, 25.0, 5.0, 1))
        , config
    );

    EXPECT_FALSE(retained);
    ASSERT_EQ(support.supports.size(), 1u);
    EXPECT_DOUBLE_EQ(support.supports[0].complete_metrics.impedance, 10.0);
}

TEST(DayPathRetention, EnforcementRemovesSupportDominatedAlternatives) {
    const auto dominant_signature = signature(ZoneId{ 1 }, ZoneId{ 2 });
    const auto dominated_signature = signature(ZoneId{ 1 }, ZoneId{ 3 });
    DayPathRetention retention;
    insert_alternative(
          retention
        , alternative_with_support_metrics(
              dominant_signature
            , { metrics(10.0, 20.0, 5.0, 0) }
          )
    );
    insert_alternative(
          retention
        , alternative_with_support_metrics(
              dominated_signature
            , { metrics(9.0, 21.0, 6.0, 1) }
          )
    );

    const auto enforced = enforce_day_path_retention(
          retention
        , DayPathRetentionConfig{}
    );

    ASSERT_TRUE(enforced);
    EXPECT_EQ(day_path_retention_size(retention), 1u);
    EXPECT_NE(
          retention.alternatives_by_signature.find(dominant_signature)
        , retention.alternatives_by_signature.end()
    );
}

TEST(DayPathRetention, BoundedAlternativeRetentionDropsWorstNondominatedPath) {
    const auto better_signature = signature(ZoneId{ 1 }, ZoneId{ 2 });
    const auto worse_signature = signature(ZoneId{ 1 }, ZoneId{ 3 });
    DayPathRetention retention;
    insert_alternative(
          retention
        , alternative_with_support_metrics(
              better_signature
            , { metrics(0.0, 30.0, 5.0, 1) }
          )
    );
    insert_alternative(
          retention
        , alternative_with_support_metrics(
              worse_signature
            , { metrics(0.0, 20.0, 6.0, 0) }
          )
    );
    const auto config = DayPathRetentionConfig{
          .limit_policy = DayPathRetentionLimitPolicy::BoundedDropWorst
        , .max_alternatives_per_od = std::size_t{ 1 }
    };

    const auto enforced = enforce_day_path_retention(retention, config);

    ASSERT_TRUE(enforced);
    EXPECT_EQ(day_path_retention_size(retention), 1u);
    EXPECT_NE(
          retention.alternatives_by_signature.find(better_signature)
        , retention.alternatives_by_signature.end()
    );
}

}  // namespace timetable::domain::assignment
