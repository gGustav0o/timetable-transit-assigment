#include "timetable/domain/assignment/split/choice_weight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <mathfp/core/error.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/numeric.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] double positive_log_argument(
            double value
        ) noexcept {
            return std::max(value, numeric::positive_stability_floor());
        }

        [[nodiscard]] double log_independence_weight(
            SplitIndependenceWeight independence
        ) noexcept {
            return std::log(positive_log_argument(independence.get()));
        }

        [[nodiscard]] SplitLogWeight kirchhoff_log_weight(
              const SplitChoiceModelConfig& model
            , SplitTransformedImpedance impedance
            , SplitIndependenceWeight   independence
        ) noexcept {
            const auto exponent = mathfp::units::as_dimless(model.exponent);
            return SplitLogWeight{
                log_independence_weight(independence)
                    - exponent * std::log(positive_log_argument(impedance.get()))
            };
        }

        [[nodiscard]] SplitLogWeight logit_log_weight(
              const SplitChoiceModelConfig& model
            , SplitTransformedImpedance impedance
            , SplitIndependenceWeight   independence
        ) noexcept {
            const auto exponent = mathfp::units::as_dimless(model.exponent);
            return SplitLogWeight{
                log_independence_weight(independence) - exponent * impedance.get()
            };
        }

        [[nodiscard]] SplitLogWeight transformed_impedance_log_weight(
              const SplitChoiceModelConfig& model
            , SplitTransformedImpedance impedance
            , SplitIndependenceWeight   independence
        ) noexcept {
            const auto exponent = mathfp::units::as_dimless(model.exponent);
            return SplitLogWeight{
                log_independence_weight(independence) - exponent * impedance.get()
            };
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_supported_split_choice_model(
        SplitChoiceModel model
    ) {
        if (model == SplitChoiceModel::Lohse) {
            return mathfp::unexpected(
                mathfp::invalid_arg("Lohse split choice model is not implemented")
                    .ctx("choice_model", std::string(to_string(model)))
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<SplitLogWeight> split_choice_log_weight(
          const SplitChoiceModelConfig& model
        , SplitTransformedImpedance      transformed_impedance
        , SplitIndependenceWeight       independence
    ) {
        if (!std::isfinite(transformed_impedance.get())) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split choice impedance must be finite")
                    .ctx("choice_model", std::string(to_string(model.model)))
                    .ctx("impedance", transformed_impedance.get())
            );
        }

        if (!std::isfinite(independence.get()) || independence.get() <= 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split choice independence must be finite and positive")
                    .ctx("choice_model", std::string(to_string(model.model)))
                    .ctx("independence", independence.get())
            );
        }

        const auto exponent = mathfp::units::as_dimless(model.exponent);
        if (!std::isfinite(exponent) || exponent <= 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split choice model exponent must be finite and positive")
                    .ctx("choice_model", std::string(to_string(model.model)))
                    .ctx("exponent", exponent)
            );
        }

        const auto model_validation = validate_supported_split_choice_model(model.model);
        if (!model_validation) {
            return mathfp::unexpected(model_validation.error());
        }

        switch (model.model) {
            case SplitChoiceModel::Kirchhoff:
                return kirchhoff_log_weight(model, transformed_impedance, independence);

            case SplitChoiceModel::Logit:
                return logit_log_weight(model, transformed_impedance, independence);

            case SplitChoiceModel::BoxCox:
                return transformed_impedance_log_weight(model, transformed_impedance, independence);

            case SplitChoiceModel::Lohse:
                return mathfp::unexpected(
                    mathfp::invalid_arg("Lohse split choice model is not implemented")
                        .ctx("choice_model", std::string(to_string(model.model)))
                );
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unsupported split choice model")
                .ctx("choice_model", static_cast<std::int64_t>(model.model))
        );
    }

}  // namespace timetable::domain::assignment
