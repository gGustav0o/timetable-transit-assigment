// include/mathfp/linalg/sparse_build.hpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/context.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/core/utility.hpp>
#include <mathfp/linalg/eigen_fwd.hpp>
#include <mathfp/linalg/types.hpp>

namespace mathfp::linalg {

    enum class DuplicatePolicy : unsigned char {
        Sum    // Eigen default behavior: duplicates are summed (or with custom functor)
        , Last   // keep last (implemented by custom merge functor: overwrite)
        , Error  // reject duplicates
    };

    namespace detail {

        inline constexpr std::string_view kPolicy = "duplicate_policy";

        MATHFP_NODISCARD inline std::string_view to_string(DuplicatePolicy p) noexcept {
            switch (p) {
            case DuplicatePolicy::Sum:   return "Sum";
            case DuplicatePolicy::Last:  return "Last";
            case DuplicatePolicy::Error: return "Error";
            }
            return "Sum";
        }

        struct PairHash {
            std::size_t operator()(const std::pair<EigenIndex, EigenIndex>& p) const noexcept {
                const auto h1 = std::hash<EigenIndex>{}(p.first);
                const auto h2 = std::hash<EigenIndex>{}(p.second);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            }
        };

        template <class StorageIndex>
        MATHFP_NODISCARD inline bool in_bounds(StorageIndex i, EigenIndex n) noexcept {
            if constexpr (std::is_signed_v<StorageIndex>) {
                if (i < 0) return false;
            }
            return static_cast<EigenIndex>(i) >= 0 && static_cast<EigenIndex>(i) < n;
        }

        template <class Trip>
        MATHFP_NODISCARD inline EigenIndex t_row(const Trip& t) {
            return static_cast<EigenIndex>(t.row());
        }
        template <class Trip>
        MATHFP_NODISCARD inline EigenIndex t_col(const Trip& t) {
            return static_cast<EigenIndex>(t.col());
        }

        template <class Scalar, class StorageIndex>
        MATHFP_NODISCARD inline auto overwrite_duplicates() {
            return [](const Scalar&, const Scalar& neu) { return neu; };
        }

    }  // namespace detail

    // build_sparse: validated SparseMatrix construction from triplets (COO).
    // - rows/cols must be positive (or at least non-negative depending on taste; we enforce >0 for usability).
    // - indices must be in bounds.
    // - duplicate handling is explicit via policy.
    // Notes:
    // - For policy Sum: Eigen sums duplicates by default.
    // - For policy Last: we provide a merge functor that overwrites.
    // - For policy Error: we detect duplicates and fail with InvalidArg.
    template <
        class Scalar
        , int Options = Eigen::ColMajor
        , class StorageIndex = int
        , std::ranges::input_range Triplets
    >
    MATHFP_NODISCARD inline ::mathfp::Expected<SpMat<Scalar, Options, StorageIndex>> build_sparse(
        EigenIndex rows
        , EigenIndex cols
        , Triplets&& triplets
        , DuplicatePolicy policy = DuplicatePolicy::Sum
        , std::source_location where = std::source_location::current()
    ) requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Triplets>>, Triplet<Scalar, StorageIndex>>
    {
        if (rows <= 0 || cols <= 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("sparse matrix shape must be positive", where)
                .ctx(ctx_key::kRows, rows)
                .ctx(ctx_key::kCols, cols)
                .ctx(detail::kPolicy, detail::to_string(policy)));
        }

        std::unordered_set<std::pair<EigenIndex, EigenIndex>, detail::PairHash> seen;
        if (policy == DuplicatePolicy::Error) {
            if constexpr (std::ranges::sized_range<Triplets>) {
                seen.reserve(static_cast<std::size_t>(std::ranges::size(triplets)) * 2);
            }
        }

        std::size_t nnz = 0;
        for (const auto& t : triplets) {
            ++nnz;

            const auto r = detail::t_row(t);
            const auto c = detail::t_col(t);

            const bool ok_r = (r >= 0 && r < rows);
            const bool ok_c = (c >= 0 && c < cols);
            if (!ok_r || !ok_c) {
                return ::mathfp::unexpected(
                    ::mathfp::invalid_arg("triplet index out of bounds", where)
                    .ctx("triplet.row", r)
                    .ctx("triplet.col", c)
                    .ctx(ctx_key::kRows, rows)
                    .ctx(ctx_key::kCols, cols)
                    .ctx("nnz_seen", nnz)
                    .ctx(detail::kPolicy, detail::to_string(policy)));
            }

            if (policy == DuplicatePolicy::Error) {
                const auto key = std::pair<EigenIndex, EigenIndex>{ r, c };
                if (!seen.insert(key).second) {
                    return ::mathfp::unexpected(
                        ::mathfp::invalid_arg("duplicate triplet entry", where)
                        .ctx("row", r)
                        .ctx("col", c)
                        .ctx("nnz_seen", nnz)
                        .ctx(detail::kPolicy, detail::to_string(policy)));
                }
            }
        }

        SpMat<Scalar, Options, StorageIndex> A(rows, cols);
        if constexpr (std::ranges::forward_range<Triplets>) {
            if (policy == DuplicatePolicy::Last) {
                A.setFromTriplets(std::ranges::begin(triplets), std::ranges::end(triplets),
                    detail::overwrite_duplicates<Scalar, StorageIndex>());
            }
            else {
                A.setFromTriplets(std::ranges::begin(triplets), std::ranges::end(triplets));
            }
        }
        else {
            // input_range: materialize
            std::vector<Triplet<Scalar, StorageIndex>> tmp;
            tmp.reserve(nnz);
            for (const auto& t : triplets) tmp.push_back(t);

            if (policy == DuplicatePolicy::Last) {
                A.setFromTriplets(tmp.begin(), tmp.end(),
                    detail::overwrite_duplicates<Scalar, StorageIndex>());
            }
            else {
                A.setFromTriplets(tmp.begin(), tmp.end());
            }
        }

        A.makeCompressed();
        return A;
    }

}  // namespace mathfp::linalg
