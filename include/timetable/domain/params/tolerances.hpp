#pragma once

#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    /**
     * @brief Tolerances applied during the search at intermediate nodes.
     *
     * Used to prune dominated connections early while keeping diversity.
     *
     * Conditions use min values among known connections to a node:
     *  - IMP(c*) <= imp_mult * min IMP(c) + imp_add
     *  - JT(c*)  <= jt_mult  * min JT(c)  + jt_add
     *  - NT(c*)  <= nt_mult  * min NT(c)  + nt_add
     */
    struct SearchTolerances final {
        Dimless imp_mult{};
        Dimless imp_add{};
        Dimless jt_mult{};
        Dimless jt_add{};
        Dimless nt_mult{};
        Dimless nt_add{};
    };

    /**
     * @brief Tolerances applied at the final connection choice step.
     *
     * Typically stricter than SearchTolerances.
     *
     * Conditions use min values among all connections to the destination:
     *  - IMP(c) <= imp_mult * min IMP + imp_add
     *  - JT(c)  <= jt_mult  * min JT  + jt_add
     *  - NT(c)  <= nt_mult  * min NT  + nt_add
     */
    //tex:
    // Paper connection-choice tolerances are stricter whole-connection rules:
    // $$IMP(c)\le p_1\min_{c'\in C_{od}}IMP(c')+p_2.$$
    // $$JT(c)\le q_1\min_{c'\in C_{od}}JT(c')+q_2.$$
    // $$NT(c)\le r_1\min_{c'\in C_{od}}NT(c')+r_2.$$
    // They are intentionally separate from the tree-local $$C_y$$ tolerances:
    // choice sees complete OD connections after search has generated candidates.
    struct ChoiceTolerances final {
        Dimless imp_mult{};
        Dimless imp_add{};
        Dimless jt_mult{};
        Dimless jt_add{};
        Dimless nt_mult{};
        Dimless nt_add{};
    };

}  // namespace timetable::domain
