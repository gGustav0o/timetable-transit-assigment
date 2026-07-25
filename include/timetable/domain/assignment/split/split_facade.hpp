#pragma once

#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/split_kernel.hpp"
#include "timetable/domain/assignment/split/types.hpp"
#include "timetable/domain/params/split.hpp"

namespace timetable::domain::assignment {

    /**
     * Тонкий фасад для разбиения спроса
     * Валидация входных данных и вызов чистого ядра
     */
    struct SplitFacadePolicy {
        SplitKernelPolicy kernel;
        SplitParams split_params; // Для обратной совместимости
    };

    /**
     * Результат работы фасада разбиения
     */
    struct SplitFacadeResult final {
        std::vector<SplitProbabilityMass> probabilities{};
        std::vector<SplitPassengerMass>   passengers{};
        std::vector<double> segment_loads{};
        std::vector<double> stop_loads{};
        std::vector<double> trip_loads{};
        std::size_t total_alternatives{};
        std::size_t significant_alternatives{};
        std::size_t suppressed_alternatives{};
        double total_demand{};
        double total_load{};
        bool validation_success{};
    };

    /**
     * Тонкий фасад для разбиения спроса
     *
     * @param log_weights вектор логарифмических весов альтернатив
     * @param demand пассажирский спрос
     * @param trip_indices индексы рейсов для каждой альтернативы
     * @param stop_indices индексы остановок для каждой альтернативы
     * @param segment_indices индексы сегментов для каждой альтернативы
     * @param policy конфигурация фасада
     */
    [[nodiscard]] mathfp::Expected<SplitFacadeResult> split_demand(
          std::span<const SplitLogWeight> log_weights
        , SplitDemandMass                  demand
        , std::span<const std::size_t>    trip_indices
        , std::span<const std::size_t>    stop_indices
        , std::span<const std::size_t>    segment_indices
        , const SplitFacadePolicy&         policy
    );

}  // namespace timetable::domain::assignment
