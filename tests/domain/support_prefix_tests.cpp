#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {

TEST(OdDaySupportPrefix, AppendingSegmentsBuildsImmutablePersistentPrefix) {
    const auto first = append_od_day_support_segment(
          OdDaySupportPrefix{}
        , ConnectionSegmentId{ 1 }
    );
    const auto second = append_od_day_support_segment(
          first
        , ConnectionSegmentId{ 2 }
    );

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    const auto first_segments = materialize_od_day_support_segments(first);
    const auto second_segments = materialize_od_day_support_segments(second);

    ASSERT_EQ(first_segments.size(), 1u);
    EXPECT_EQ(first_segments[0], ConnectionSegmentId{ 1 });

    ASSERT_EQ(second_segments.size(), 2u);
    EXPECT_EQ(second_segments[0], ConnectionSegmentId{ 1 });
    EXPECT_EQ(second_segments[1], ConnectionSegmentId{ 2 });
}

TEST(OdDayPathPrefix, AppendingLegsKeepsEarlierPrefixReusable) {
    const auto origin = ZoneId{ 7 };
    const auto initial = make_od_day_path_prefix(origin);
    const auto first = append_od_day_path_leg(
          initial
        , DayPathLeg{
              .kind          = ConnectionLegKind::AccessWalk
            , .physical_from = endpoint_key(origin)
            , .physical_to   = endpoint_key(StopId{ 1 })
          }
    );
    const auto second = append_od_day_path_leg(
          first
        , DayPathLeg{
              .kind          = ConnectionLegKind::Ride
            , .physical_from = endpoint_key(StopId{ 1 })
            , .physical_to   = endpoint_key(StopId{ 2 })
            , .line          = LineId{ 3 }
            , .route         = RouteId{ 4 }
          }
    );

    const auto first_materialized = materialize_day_path_prefix(first);
    const auto second_materialized = materialize_day_path_prefix(second);

    EXPECT_EQ(first_materialized.origin, origin);
    ASSERT_EQ(first_materialized.legs.size(), 1u);
    EXPECT_EQ(first_materialized.legs[0].kind, ConnectionLegKind::AccessWalk);

    EXPECT_EQ(second_materialized.origin, origin);
    ASSERT_EQ(second_materialized.legs.size(), 2u);
    EXPECT_EQ(second_materialized.legs[0].kind, ConnectionLegKind::AccessWalk);
    EXPECT_EQ(second_materialized.legs[1].kind, ConnectionLegKind::Ride);
}

}  // namespace timetable::domain::assignment
