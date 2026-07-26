#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    class ToleranceMultiplier final {
    public:
        constexpr ToleranceMultiplier() = default;

        [[nodiscard]] constexpr double value() const noexcept {
            return value_;
        }

    private:
        constexpr explicit ToleranceMultiplier(double value) noexcept
            : value_(value) {}

        double value_{};

        friend mathfp::Expected<ToleranceMultiplier> make_tolerance_multiplier(
              Dimless
            , const char*
        );
    };

    class ImpedanceTolerance final {
    public:
        constexpr ImpedanceTolerance() = default;

        [[nodiscard]] constexpr double value() const noexcept {
            return value_;
        }

    private:
        constexpr explicit ImpedanceTolerance(double value) noexcept
            : value_(value) {}

        double value_{};

        friend mathfp::Expected<ImpedanceTolerance> make_impedance_tolerance(
              Dimless
            , const char*
        );
    };

    class JourneyTimeTolerance final {
    public:
        constexpr JourneyTimeTolerance() = default;

        [[nodiscard]] constexpr Time value() const noexcept {
            return value_;
        }

        [[nodiscard]] constexpr double seconds() const noexcept {
            return value_.value();
        }

    private:
        constexpr explicit JourneyTimeTolerance(Time value) noexcept
            : value_(value) {}

        Time value_{};

        friend mathfp::Expected<JourneyTimeTolerance> make_journey_time_tolerance(
              Time
            , const char*
        );
    };

    class TransferCountTolerance final {
    public:
        constexpr TransferCountTolerance() = default;

        [[nodiscard]] constexpr double value() const noexcept {
            return value_;
        }

    private:
        constexpr explicit TransferCountTolerance(double value) noexcept
            : value_(value) {}

        double value_{};

        friend mathfp::Expected<TransferCountTolerance> make_transfer_count_tolerance(
              Dimless
            , const char*
        );
    };

    [[nodiscard]] constexpr bool operator==(
          ToleranceMultiplier lhs
        , ToleranceMultiplier rhs
    ) noexcept {
        return lhs.value() == rhs.value();
    }

    [[nodiscard]] constexpr bool operator==(
          ImpedanceTolerance lhs
        , ImpedanceTolerance rhs
    ) noexcept {
        return lhs.value() == rhs.value();
    }

    [[nodiscard]] constexpr bool operator==(
          JourneyTimeTolerance lhs
        , JourneyTimeTolerance rhs
    ) noexcept {
        return lhs.value() == rhs.value();
    }

    [[nodiscard]] constexpr bool operator==(
          TransferCountTolerance lhs
        , TransferCountTolerance rhs
    ) noexcept {
        return lhs.value() == rhs.value();
    }

    mathfp::Expected<ToleranceMultiplier> make_tolerance_multiplier(
          Dimless     value
        , const char* name
    );

    mathfp::Expected<ImpedanceTolerance> make_impedance_tolerance(
          Dimless     value
        , const char* name
    );

    mathfp::Expected<JourneyTimeTolerance> make_journey_time_tolerance(
          Time        value
        , const char* name
    );

    mathfp::Expected<TransferCountTolerance> make_transfer_count_tolerance(
          Dimless     value
        , const char* name
    );

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
        ToleranceMultiplier  imp_mult{};
        ImpedanceTolerance   imp_add{};
        ToleranceMultiplier  jt_mult{};
        JourneyTimeTolerance jt_add{};
        ToleranceMultiplier  nt_mult{};
        TransferCountTolerance nt_add{};
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
    // Connection-choice tolerances are stricter whole-connection rules:
    // $$IMP(c)\le p_1\min_{c'\in C_{od}}IMP(c')+p_2.$$
    // $$JT(c)\le q_1\min_{c'\in C_{od}}JT(c')+q_2.$$
    // $$NT(c)\le r_1\min_{c'\in C_{od}}NT(c')+r_2.$$
    // They are intentionally separate from the tree-local $$C_y$$ tolerances:
    // choice sees complete OD connections after search has generated candidates.
    struct ChoiceTolerances final {
        ToleranceMultiplier    imp_mult{};
        ImpedanceTolerance     imp_add{};
        ToleranceMultiplier    jt_mult{};
        JourneyTimeTolerance   jt_add{};
        ToleranceMultiplier    nt_mult{};
        TransferCountTolerance nt_add{};
    };

}  // namespace timetable::domain
