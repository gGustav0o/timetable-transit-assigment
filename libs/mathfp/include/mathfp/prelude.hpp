#pragma once

#include <mathfp/core/unit.hpp>
#include <mathfp/core/context.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/utility.hpp>

#include <mathfp/core/fp.hpp>
#include <mathfp/core/applicative.hpp>
#include <mathfp/core/traverse.hpp>

namespace mathfp::prelude {

    // core
    using ::mathfp::Unit;
    using ::mathfp::kUnit;

    using ::mathfp::Context;

    using ::mathfp::ErrKind;
    using ::mathfp::Error;

    using ::mathfp::Expected;
    using ::mathfp::Unexpected;
    using ::mathfp::unexpected;

    // error DSL
    using ::mathfp::make_error;
    using ::mathfp::domain_error;
    using ::mathfp::invalid_arg;
    using ::mathfp::non_convergence;
    using ::mathfp::singular;
    using ::mathfp::ill_conditioned;
    using ::mathfp::overflow_error;
    using ::mathfp::underflow_error;
    using ::mathfp::precision_loss;
    using ::mathfp::not_implemented;
    using ::mathfp::internal_error;

    // ensure-helpers
    using ::mathfp::ensure;
    using ::mathfp::ensure_eq;
    using ::mathfp::ensure_le;
    using ::mathfp::ensure_ge;
    using ::mathfp::ensure_lt;
    using ::mathfp::ensure_gt;
    using ::mathfp::ensure_nonzero;
    using ::mathfp::ensure_positive;

    // fp (������� �������)
    using ::mathfp::fp::map;
    using ::mathfp::fp::and_then;
    using ::mathfp::fp::map_error;
    using ::mathfp::fp::or_else;
    using ::mathfp::fp::inspect;
    using ::mathfp::fp::inspect_error;
    using ::mathfp::fp::flatten;
    using ::mathfp::fp::value_or;
    using ::mathfp::fp::value_or_else;

    // pipe (�������� � operator|)
    using ::mathfp::fp::pipe::operator|;
    using ::mathfp::fp::pipe::map;
    using ::mathfp::fp::pipe::and_then;
    using ::mathfp::fp::pipe::map_error;
    using ::mathfp::fp::pipe::or_else;
    using ::mathfp::fp::pipe::inspect;
    using ::mathfp::fp::pipe::inspect_error;

    // applicative + traverse
    using ::mathfp::app::product;
    using ::mathfp::app::ap;
    using ::mathfp::app::lift2;
    using ::mathfp::app::lift3;

    using ::mathfp::trv::traverse;
    using ::mathfp::trv::sequence;

    // types
    using ::mathfp::StrongType;
    using ::mathfp::is_strong_type_v;
    using ::mathfp::map_strong;

    using ::mathfp::Index;
    using ::mathfp::invalid_index;
    using ::mathfp::is_valid;
    using ::mathfp::to_usize;
    using ::mathfp::make_index;
    using ::mathfp::next_index;
    using ::mathfp::prev_index;

    namespace units = ::mathfp::units;

    using ::mathfp::units::Dim;
    using ::mathfp::units::Dimless;
    using ::mathfp::units::Quantity;

    using ::mathfp::units::Length;
    using ::mathfp::units::Mass;
    using ::mathfp::units::Time;

    using ::mathfp::units::meters;
    using ::mathfp::units::seconds;
    using ::mathfp::units::kilograms;
    using ::mathfp::units::dimless;

    namespace ranges = ::mathfp::ranges;

    using ::mathfp::ranges::zip;
    using ::mathfp::ranges::zip_with;

}  // namespace mathfp::prelude
