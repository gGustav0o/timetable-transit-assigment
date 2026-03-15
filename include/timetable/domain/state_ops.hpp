#pragma once

#include <cstddef>
#include <utility>

#include <mathfp/core/expected.hpp>

namespace timetable::domain::state_ops {

    template <typename State, typename Step>
    [[nodiscard]] auto transition(
        State&& state
        , Step&& step
    ) -> decltype(step(std::forward<State>(state))) {
        return step(std::forward<State>(state));
    }

    template <typename ExpectedState, typename Step>
    [[nodiscard]] auto bind(
        ExpectedState&& state
        , Step&& step
    ) -> decltype(step(std::move(*state))) {
        auto current = std::forward<ExpectedState>(state);
        if (!current) {
            return mathfp::unexpected(current.error());
        }

        return step(std::move(*current));
    }

    template <typename State, typename Range, typename Step>
    [[nodiscard]] mathfp::Expected<State> fold(
        State state
        , Range&& range
        , Step&& step
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
        State state
        , std::size_t count
        , Step&& step
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

    template <typename State, typename Step, typename ThrowFn>
    void transition_or_throw(
        State& state
        , Step&& step
        , ThrowFn&& throw_error
    ) {
        auto next_state = transition(std::move(state), std::forward<Step>(step));
        if (!next_state) {
            throw_error(next_state.error());
        }

        state = std::move(*next_state);
    }

}  // namespace timetable::domain::state_ops
