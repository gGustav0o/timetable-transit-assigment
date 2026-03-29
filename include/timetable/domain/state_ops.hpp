#pragma once

#include <cstddef>
#include <utility>

#include <mathfp/core/expected.hpp>

namespace timetable::domain::state_ops {

    // Stateful folds live here because they thread an evolving builder/state
    // through a collection. Plain Expected composition should use mathfp::fp.

    template <typename State, typename Range, typename Step>
    [[nodiscard]] mathfp::Expected<State> fold(
          State   state
        , Range&& range
        , Step&&  step
    ) {
        for (auto&& input : range) {
            auto next_state = step(std::move(state), input);
            if (!next_state) {
                return mathfp::unexpected(next_state.error());
            }
            state = std::move(*next_state);
        }

        return state;
    }

    template <typename State, typename Step>
    [[nodiscard]] mathfp::Expected<State> fold_indexed(
          State       state
        , std::size_t count
        , Step&&      step
    ) {
        for (std::size_t index = 0; index < count; ++index) {
            auto next_state = step(std::move(state), index);
            if (!next_state) {
                return mathfp::unexpected(next_state.error());
            }
            state = std::move(*next_state);
        }

        return state;
    }

}  // namespace timetable::domain::state_ops
