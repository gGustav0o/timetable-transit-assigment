#pragma once

#include <mathfp/core/numeric_tolerance.hpp>

#include <Eigen/Dense>

namespace mathfp {
    template<class Derived>
    MATHFP_PURE
    inline double matrix_norm_inf(const Eigen::MatrixBase<Derived>& A) {
        return A.cwiseAbs().rowwise().sum().maxCoeff();
    }

    template<class Derived>
    MATHFP_PURE
    inline double matrix_abs_tolerance(const Eigen::MatrixBase<Derived>& A) {
        constexpr double eps = mathfp::machine_epsilon<double>();
        const double s = (std::max)(1.0, matrix_norm_inf(A));
        const auto N = static_cast<double>(A.cols());
        return eps * s * N;
    }

    template<class DA, class DB>
    MATHFP_PURE
    inline double matrix_abs_tolerance(
        const Eigen::MatrixBase<DA>& A
        , const Eigen::MatrixBase<DB>& B
    ) {
        constexpr double eps = mathfp::machine_epsilon<double>();
        const double s = (std::max)({ 1.0, mathfp::matrix_norm_inf(A), mathfp::matrix_norm_inf(B) });
        const auto N = static_cast<double>((std::max)(A.cols(), B.cols()));
        return eps * s * N;
    }

    template<class Derived>
    MATHFP_PURE
    inline double spectral_abs_tolerance_sym(const Eigen::MatrixBase<Derived>& A) {
        using Mat = typename Derived::PlainObject;
        Eigen::SelfAdjointEigenSolver<Mat> es(A.derived(), Eigen::EigenvaluesOnly);
        if (es.info() != Eigen::Success) {
            return mathfp::matrix_abs_tolerance(A);

        }
        const auto& evals = es.eigenvalues();
        const double lam0 = std::abs(evals(0));
        const double lam1 = std::abs(evals(evals.size() - 1));
        const double lam_abs_max = (std::max)(lam0, lam1);
        return mathfp::machine_epsilon<double>() * (std::max)(1.0, lam_abs_max);
    }

    template<class Derived>
    MATHFP_PURE
    inline double psd_jitter(const Eigen::MatrixBase<Derived>& A) {
        return mathfp::matrix_abs_tolerance(A);
    }
}
