#include <gtest/gtest.h>

#include "timetable/domain/assignment/od_day_path_contract.hpp"

namespace timetable::domain::assignment {

TEST(OdDayPathSearchContract, ProductionContractRequiresDeclaredOrigins) {
    EXPECT_FALSE(satisfies_od_day_path_search_contract(
        make_od_day_path_search_contract(0u)
    ));
    EXPECT_TRUE(satisfies_od_day_path_search_contract(
        make_od_day_path_search_contract(3u)
    ));
}

TEST(OdDayPathSearchContract, ProductionContractIsServiceDayOriginTreeModel) {
    const auto contract = make_od_day_path_search_contract(2u);

    EXPECT_EQ(contract.horizon, OdDayPathSearchHorizon::ServiceDay);
    EXPECT_EQ(
          contract.tree_contract
        , OdDayPathTreeContract::OneTreePerDeclaredOrigin
    );
    EXPECT_EQ(
          contract.result_projection_policy
        , OdDayPathResultProjectionPolicy::DayPathPostLayer
    );
    EXPECT_EQ(
          contract.feasibility_semantics
        , OdDayPathFeasibilitySemantics::TimetableTemporalSuitability
    );
}

}  // namespace timetable::domain::assignment
