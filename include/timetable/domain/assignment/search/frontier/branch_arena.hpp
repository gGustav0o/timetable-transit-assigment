#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "timetable/domain/assignment/search/model/branch.hpp"

namespace timetable::domain::assignment {

    struct BranchSlot final {
        std::unique_ptr<SearchBranch> branch{};
        std::optional<std::size_t>    parent{};
        std::size_t                   live_children{};
        bool                          self_released{};
    };

    using BranchArena = std::deque<BranchSlot>;

    [[nodiscard]] inline const SearchBranch& branch_at(
          const BranchArena& branches
        , std::size_t        index
    ) {
        return *branches.at(index).branch;
    }

    [[nodiscard]] inline SearchBranch& branch_at(
          BranchArena& branches
        , std::size_t  index
    ) {
        return *branches.at(index).branch;
    }

    inline std::size_t append_branch(
          BranchArena& branches
        , SearchBranch branch
    ) {
        const auto parent = branch.trace.parent_branch;
        if (parent.has_value()) {
            ++branches.at(*parent).live_children;
        }
        branches.push_back(
            BranchSlot{
                  .branch = std::make_unique<SearchBranch>(std::move(branch))
                , .parent = parent
            }
        );
        return branches.size() - 1u;
    }

    template <typename ReleasePayload>
    void release_branch_if_closed(
          BranchArena&     branches
        , std::size_t      index
        , ReleasePayload&& release_payload
    ) {
        auto&& release = release_payload;
        auto cursor = std::optional<std::size_t>{ index };
        while (cursor.has_value()) {
            auto& slot = branches.at(*cursor);
            slot.self_released = true;
            if (slot.live_children != 0u || slot.branch == nullptr) {
                return;
            }

            const auto parent = slot.parent;
            slot.branch.reset();
            release(*cursor);

            if (!parent.has_value()) {
                return;
            }

            auto& parent_slot = branches.at(*parent);
            if (parent_slot.live_children == 0u) {
                return;
            }
            --parent_slot.live_children;
            if (!parent_slot.self_released) {
                return;
            }
            cursor = parent;
        }
    }

    struct LevelFrontierBuffer final {
        std::vector<std::size_t> entries{};
        std::size_t              head{};

        [[nodiscard]] bool empty() const noexcept {
            return head >= entries.size();
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return empty() ? 0u : entries.size() - head;
        }

        [[nodiscard]] std::size_t front() const {
            return entries.at(head);
        }

        void pop_front() noexcept {
            if (head < entries.size()) {
                ++head;
            }
        }

        void push_back(std::size_t value) {
            entries.push_back(value);
        }

        void clear() noexcept {
            entries.clear();
            head = 0u;
        }

        void swap(LevelFrontierBuffer& rhs) noexcept {
            entries.swap(rhs.entries);
            std::swap(head, rhs.head);
        }
    };

}  // namespace timetable::domain::assignment
