#pragma once

#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/linalg/checks.hpp>
#include <mathfp/linalg/eigen_fwd.hpp>
#include <mathfp/linalg/types.hpp>

#include <Eigen/Cholesky>
#include <Eigen/LU>
#include <Eigen/QR>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <Eigen/IterativeLinearSolvers>

namespace mathfp::linalg {

    struct IterativeOptions final {
        int max_iter = 500;
        double tol   = 1e-10;
    };

    namespace detail {

        inline constexpr std::string_view kMethod    = "method";
        inline constexpr std::string_view kEigenInfo = "eigen_info";

        MATHFP_NODISCARD inline std::string_view eigen_info_to_string(Eigen::ComputationInfo info) noexcept {
            switch (info) {
            case Eigen::Success:        return "Success";
            case Eigen::NumericalIssue: return "NumericalIssue";
            case Eigen::NoConvergence:  return "NoConvergence";
            case Eigen::InvalidInput:   return "InvalidInput";
            }
            return "Unknown";
        }

        MATHFP_NODISCARD inline ::mathfp::Error map_eigen_info(
            Eigen::ComputationInfo info
            , std::string_view msg
            , std::source_location where
        ) {
            switch (info) {
            case Eigen::Success:
                return ::mathfp::internal_error("map_eigen_info called on Success", where);

            case Eigen::InvalidInput:
                return ::mathfp::invalid_arg(msg, where).ctx(kEigenInfo, eigen_info_to_string(info));

            case Eigen::NoConvergence:
                return ::mathfp::non_convergence(msg, where).ctx(kEigenInfo, eigen_info_to_string(info));

            case Eigen::NumericalIssue:
                // Eigen doesn't distinguish singular vs ill-conditioned reliably across solvers.
                // We choose IllConditioned as the safer interpretation.
                return ::mathfp::ill_conditioned(msg, where).ctx(kEigenInfo, eigen_info_to_string(info));
            }
            return ::mathfp::internal_error(msg, where).ctx(kEigenInfo, eigen_info_to_string(info));
        }

    }  // namespace detail

    // -------------------- Dense: SPD solve via LLT -------------------------------
    // Assumes A is symmetric positive definite (SPD). If not, NumericalIssue.
    template <class Scalar>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_spd(
        MatRef<Scalar> A
        , VecRef<Scalar> b
        , std::source_location where = std::source_location::current()
    ) {
        MATHFP_TRY(ensure_nonempty(A, where));
        MATHFP_TRY(ensure_square(A, where));
        MATHFP_TRY(ensure_matvec_compatible(A, b, where));

        Eigen::LLT<Mat<Scalar>> llt(A);
        if (llt.info() != Eigen::Success) {
            return ::mathfp::unexpected(
                detail::map_eigen_info(llt.info(), "LLT decomposition failed", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_spd"));
        }

        Vec<Scalar> x = llt.solve(b);
        return x;
    }

    // -------------------- Dense: general solve via FullPivLU ---------------------
    // Robust singularity detection; slower but stable contract.
    template <class Scalar>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_lu(
        MatRef<Scalar> A
        , VecRef<Scalar> b
        , std::source_location where = std::source_location::current()
    ) {
        MATHFP_TRY(ensure_nonempty(A, where));
        MATHFP_TRY(ensure_square(A, where));
        MATHFP_TRY(ensure_matvec_compatible(A, b, where));

        Eigen::FullPivLU<Mat<Scalar>> lu(A);
        if (!lu.isInvertible()) {
            return ::mathfp::unexpected(
                ::mathfp::singular("matrix is singular", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_lu")
                .ctx("rank", lu.rank()));
        }

        Vec<Scalar> x = lu.solve(b);
        return x;
    }

    // -------------------- Dense: least-squares / rank-revealing QR ---------------
    // Useful when A is near-singular; still returns solution but can be ill-conditioned.
    template <class Scalar>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_qr(
        MatRef<Scalar> A
        , VecRef<Scalar> b
        , std::source_location where = std::source_location::current()
    ) {
        MATHFP_TRY(ensure_nonempty(A, where));
        MATHFP_TRY(ensure_square(A, where));
        MATHFP_TRY(ensure_matvec_compatible(A, b, where));

        Eigen::ColPivHouseholderQR<Mat<Scalar>> qr(A);
        const auto r = qr.rank();
        if (r < A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::singular("matrix is rank-deficient", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx("rank", r)
                .ctx(detail::kMethod, "solve_qr"));
        }

        Vec<Scalar> x = qr.solve(b);
        return x;
    }

    // -------------------- Sparse: SPD solve via SimplicialLLT --------------------
    template <class Scalar, int Options = Eigen::ColMajor, class StorageIndex = int>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_sparse_spd(
        const SpMat<Scalar, Options, StorageIndex>& A
        , VecRef<Scalar> b
        , std::source_location where = std::source_location::current()
    ) {
        if (A.rows() <= 0 || A.cols() <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("sparse matrix must be non-empty", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_spd"));
        }
        if (A.rows() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("matrix must be square", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_spd"));
        }
        if (b.size() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("dimension mismatch", where)
                .ctx("b.size", b.size())
                .ctx("A.cols", A.cols())
                .ctx(detail::kMethod, "solve_sparse_spd"));
        }

        Eigen::SimplicialLLT<SpMat<Scalar, Options, StorageIndex>> llt;
        llt.compute(A);
        if (llt.info() != Eigen::Success) {
            return ::mathfp::unexpected(
                detail::map_eigen_info(llt.info(), "SimplicialLLT failed", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_spd"));
        }

        Vec<Scalar> x = llt.solve(b);
        if (llt.info() != Eigen::Success) {
            return ::mathfp::unexpected(
                detail::map_eigen_info(llt.info(), "SimplicialLLT solve failed", where)
                .ctx(detail::kMethod, "solve_sparse_spd"));
        }
        return x;
    }

    // -------------------- Sparse: general direct solve via SparseLU --------------
    template <class Scalar, int Options = Eigen::ColMajor, class StorageIndex = int>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_sparse_lu(
        const SpMat<Scalar, Options, StorageIndex>& A
        , VecRef<Scalar> b
        , std::source_location where = std::source_location::current()
    ) {
        if (A.rows() <= 0 || A.cols() <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("sparse matrix must be non-empty", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_lu"));
        }
        if (A.rows() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("matrix must be square", where)
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_lu"));
        }
        if (b.size() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("dimension mismatch", where)
                .ctx("b.size", b.size())
                .ctx("A.cols", A.cols())
                .ctx(detail::kMethod, "solve_sparse_lu"));
        }

        Eigen::SparseLU<SpMat<Scalar, Options, StorageIndex>> lu;
        lu.analyzePattern(A);
        lu.factorize(A);

        if (lu.info() != Eigen::Success) {
            // SparseLU tends to use NumericalIssue for singular/ill-conditioned.
            return ::mathfp::unexpected(
                detail::map_eigen_info(lu.info(), "SparseLU factorization failed", where)
                .ctx(detail::kMethod, "solve_sparse_lu")
                .ctx(ctx_key::kRows, A.rows())
                .ctx(ctx_key::kCols, A.cols()));
        }

        Vec<Scalar> x = lu.solve(b);
        if (lu.info() != Eigen::Success) {
            return ::mathfp::unexpected(
                detail::map_eigen_info(lu.info(), "SparseLU solve failed", where)
                .ctx(detail::kMethod, "solve_sparse_lu"));
        }

        return x;
    }

    // -------------------- Sparse: iterative solve via ConjugateGradient ----------
    // For SPD matrices. NonConvergence with iter/tol/residual context.
    template <class Scalar, int Options = Eigen::ColMajor, class StorageIndex = int>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_sparse_cg(
        const SpMat<Scalar, Options, StorageIndex>& A
        , VecRef<Scalar> b
        , IterativeOptions opt = {}
        , std::source_location where = std::source_location::current()
    ) {
        if (A.rows() <= 0 || A.cols() <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("sparse matrix must be non-empty", where)
                .ctx(ctx_key::kRows, A.rows()).ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_cg"));
        }
        if (A.rows() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("matrix must be square", where)
                .ctx(ctx_key::kRows, A.rows()).ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_cg"));
        }
        if (b.size() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("dimension mismatch", where)
                .ctx("b.size", b.size()).ctx("A.cols", A.cols())
                .ctx(detail::kMethod, "solve_sparse_cg"));
        }
        if (opt.max_iter <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("max_iter must be positive", where)
                .ctx(ctx_key::kMaxIter, opt.max_iter)
                .ctx(detail::kMethod, "solve_sparse_cg"));
        }
        if (!(opt.tol > 0.0)) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("tol must be positive", where)
                .ctx(ctx_key::kTol, opt.tol)
                .ctx(detail::kMethod, "solve_sparse_cg"));
        }

        Eigen::ConjugateGradient<SpMat<Scalar, Options, StorageIndex>, Eigen::Lower | Eigen::Upper> cg;
        cg.setMaxIterations(opt.max_iter);
        cg.setTolerance(opt.tol);
        cg.compute(A);

        if (cg.info() != Eigen::Success) {
            return ::mathfp::unexpected(
                detail::map_eigen_info(cg.info(), "ConjugateGradient compute failed", where)
                .ctx(detail::kMethod, "solve_sparse_cg"));
        }

        Vec<Scalar> x = cg.solve(b);

        // Eigen reports status in info() after solve.
        if (cg.info() == Eigen::Success) {
            return x;
        }

        if (cg.info() == Eigen::NoConvergence) {
            return ::mathfp::unexpected(
                ::mathfp::non_convergence("iterative solver did not converge", where)
                .ctx(detail::kMethod, "solve_sparse_cg")
                .ctx(ctx_key::kIter, cg.iterations())
                .ctx(ctx_key::kMaxIter, opt.max_iter)
                .ctx(ctx_key::kTol, opt.tol)
                .ctx(ctx_key::kResidual, cg.error())
                .ctx(detail::kEigenInfo, detail::eigen_info_to_string(cg.info())));
        }

        return ::mathfp::unexpected(
            detail::map_eigen_info(cg.info(), "ConjugateGradient solve failed", where)
            .ctx(detail::kMethod, "solve_sparse_cg")
            .ctx(ctx_key::kIter, cg.iterations())
            .ctx(ctx_key::kResidual, cg.error()));
    }

    // -------------------- Sparse: iterative solve via BiCGSTAB -------------------
    // For general matrices.
    template <class Scalar, int Options = Eigen::ColMajor, class StorageIndex = int>
    MATHFP_NODISCARD inline ::mathfp::Expected<Vec<Scalar>> solve_sparse_bicgstab(
        const SpMat<Scalar, Options, StorageIndex>& A
        , VecRef<Scalar> b
        , IterativeOptions opt = {}
        , std::source_location where = std::source_location::current()
    ) {
        if (A.rows() <= 0 || A.cols() <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("sparse matrix must be non-empty", where)
                .ctx(ctx_key::kRows, A.rows()).ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_bicgstab"));
        }
        if (A.rows() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("matrix must be square", where)
                .ctx(ctx_key::kRows, A.rows()).ctx(ctx_key::kCols, A.cols())
                .ctx(detail::kMethod, "solve_sparse_bicgstab"));
        }
        if (b.size() != A.cols()) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("dimension mismatch", where)
                .ctx("b.size", b.size()).ctx("A.cols", A.cols())
                .ctx(detail::kMethod, "solve_sparse_bicgstab"));
        }
        if (opt.max_iter <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("max_iter must be positive", where)
                .ctx(ctx_key::kMaxIter, opt.max_iter)
                .ctx(detail::kMethod, "solve_sparse_bicgstab"));
        }
        if (!(opt.tol > 0.0)) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("tol must be positive", where)
                .ctx(ctx_key::kTol, opt.tol)
                .ctx(detail::kMethod, "solve_sparse_bicgstab"));
        }

        Eigen::BiCGSTAB<SpMat<Scalar, Options, StorageIndex>> bicg;
        bicg.setMaxIterations(opt.max_iter);
        bicg.setTolerance(opt.tol);
        bicg.compute(A);

        if (bicg.info() != Eigen::Success) {
            return ::mathfp::unexpected(
                detail::map_eigen_info(bicg.info(), "BiCGSTAB compute failed", where)
                .ctx(detail::kMethod, "solve_sparse_bicgstab"));
        }

        Vec<Scalar> x = bicg.solve(b);

        if (bicg.info() == Eigen::Success) {
            return x;
        }

        if (bicg.info() == Eigen::NoConvergence) {
            return ::mathfp::unexpected(
                ::mathfp::non_convergence("iterative solver did not converge", where)
                .ctx(detail::kMethod, "solve_sparse_bicgstab")
                .ctx(ctx_key::kIter, bicg.iterations())
                .ctx(ctx_key::kMaxIter, opt.max_iter)
                .ctx(ctx_key::kTol, opt.tol)
                .ctx(ctx_key::kResidual, bicg.error())
                .ctx(detail::kEigenInfo, detail::eigen_info_to_string(bicg.info())));
        }

        return ::mathfp::unexpected(
            detail::map_eigen_info(bicg.info(), "BiCGSTAB solve failed", where)
            .ctx(detail::kMethod, "solve_sparse_bicgstab")
            .ctx(ctx_key::kIter, bicg.iterations())
            .ctx(ctx_key::kResidual, bicg.error()));
    }

}  // namespace mathfp::linalg
