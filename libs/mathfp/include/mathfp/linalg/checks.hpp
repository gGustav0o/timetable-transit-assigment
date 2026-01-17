// include/mathfp/linalg/checks.hpp
#pragma once

#include <source_location>
#include <type_traits>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/core/utility.hpp>
#include <mathfp/core/context.hpp>
#include <mathfp/linalg/eigen_fwd.hpp>

namespace mathfp::linalg {

    using ::mathfp::ctx_key::kCols;
    using ::mathfp::ctx_key::kRows;

    template <class Derived>
    [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> ensure_nonempty(
        const Eigen::MatrixBase<Derived>& a
        , std::source_location where = std::source_location::current()
    ) {
        if (a.rows() > 0 && a.cols() > 0) return ::mathfp::kUnit;
        return ::mathfp::unexpected(
            ::mathfp::invalid_arg("matrix must be non-empty", where)
            .ctx(kRows, a.rows())
            .ctx(kCols, a.cols()));
    }

    template <class Derived>
    [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> ensure_square(
        const Eigen::MatrixBase<Derived>& a
        , std::source_location where = std::source_location::current()
    ) {
        if (a.rows() == a.cols()) return ::mathfp::kUnit;
        return ::mathfp::unexpected(
            ::mathfp::invalid_arg("matrix must be square", where)
            .ctx(kRows, a.rows())
            .ctx(kCols, a.cols()));
    }

    template <class A, class B>
    [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> ensure_same_shape(
        const Eigen::MatrixBase<A>& a
        , const Eigen::MatrixBase<B>& b
        , std::source_location where = std::source_location::current()
    ) {
        if (a.rows() == b.rows() && a.cols() == b.cols()) return ::mathfp::kUnit;
        return ::mathfp::unexpected(
            ::mathfp::invalid_arg("shape mismatch", where)
            .ctx("a.rows", a.rows()).ctx("a.cols", a.cols())
            .ctx("b.rows", b.rows()).ctx("b.cols", b.cols()));
    }

    template <class A, class X>
    [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> ensure_matvec_compatible(
        const Eigen::MatrixBase<A>& a
        , const Eigen::MatrixBase<X>& x
        , std::source_location where = std::source_location::current()
    ) {
        // a: (m x n), x: (n x 1) or (n)
        const auto n = a.cols();
        const auto xr = x.rows();
        const auto xc = x.cols();
        const bool ok = (xc == 1 && xr == n) || (xr == 1 && xc == n);  // allow row vec too
        if (ok) return ::mathfp::kUnit;

        return ::mathfp::unexpected(
            ::mathfp::invalid_arg("matrix-vector dimension mismatch", where)
            .ctx("a.rows", a.rows()).ctx("a.cols", a.cols())
            .ctx("x.rows", xr).ctx("x.cols", xc));
    }

    template <class Derived>
    [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> ensure_finite(
        const Eigen::MatrixBase<Derived>& a
        , std::source_location where = std::source_location::current()
    ) requires requires { a.allFinite(); } {
        if (a.allFinite()) return ::mathfp::kUnit;
        return ::mathfp::unexpected(
            ::mathfp::domain_error("matrix contains NaN/Inf", where)
            .ctx(kRows, a.rows())
            .ctx(kCols, a.cols()));
    }

}  // namespace mathfp::linalg
