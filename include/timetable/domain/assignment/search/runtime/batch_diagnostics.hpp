#pragma once

#include <chrono>
#include <cstddef>

#include "timetable/domain/assignment/search/runtime/batch_context.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/assignment/search/runtime/od_day_frontier_synchronization.hpp"
#include "timetable/domain/assignment/search/runtime/result_finalization.hpp"

namespace timetable::domain::assignment::runtime {

    class SearchBatchDiagnosticsRuntime final {
    public:
        explicit SearchBatchDiagnosticsRuntime(
            SearchBatchContext& context
        );

        [[nodiscard]] std::size_t retained_production_alternative_count() const noexcept;
        [[nodiscard]] std::size_t projection_state_count() const noexcept;
        [[nodiscard]] SearchStorageDiagnostics storage_diagnostics() const noexcept;

        void emit_initial_status() const;
        void emit_od_day_memory_limits(
            const OdDayProductionMemoryLimits& limits
        ) const;
        void emit_storage_diagnostics() const;
        void emit_wall_clock_heartbeat(
              const char* stage
            , std::size_t branch_index
        );
        void emit_search_heartbeat() const;
        void emit_batch_cancelled_before_tree() const;
        void emit_batch_cancelled_during_successor_scan() const;
        void emit_frontier_compacted(
              const char* reason
            , const OdDayFrontierCompactionResult& compaction
        ) const;
        void emit_projection_details(
            const SearchBatchFinalization& finalization
        ) const;
        void emit_batch_done(
            const SearchBatchFinalization& finalization
        ) const;

    private:
        SearchBatchContext& context_;
        std::chrono::steady_clock::time_point batch_started_at_;
        std::chrono::steady_clock::time_point last_wall_clock_heartbeat_;
    };

}  // namespace timetable::domain::assignment::runtime
