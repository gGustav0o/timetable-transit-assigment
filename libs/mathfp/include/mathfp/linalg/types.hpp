// include/mathfp/linalg/types.hpp
#pragma once

#include <type_traits>

#include <mathfp/linalg/eigen_fwd.hpp>

namespace mathfp::linalg {

	using EigenIndex = Eigen::Index;

	template <class Scalar>
	using Vec = Eigen::Matrix<Scalar, Eigen::Dynamic, 1>;

	template <class Scalar>
	using RowVec = Eigen::Matrix<Scalar, 1, Eigen::Dynamic>;

	template <class Scalar>
	using Mat = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>;

	template <class Scalar>
	using RowMat = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

	template <class Scalar, int Options = Eigen::ColMajor, class StorageIndex = int>
	using SpMat = Eigen::SparseMatrix<Scalar, Options, StorageIndex>;

	template <class Scalar, class StorageIndex = int>
	using Triplet = Eigen::Triplet<Scalar, StorageIndex>;

	template <class Scalar>
	using VecRef = Eigen::Ref<const Vec<Scalar>>;

	template <class Scalar>
	using MatRef = Eigen::Ref<const Mat<Scalar>>;

	template <class Scalar>
	using MutVecRef = Eigen::Ref<Vec<Scalar>>;

	template <class Scalar>
	using MutMatRef = Eigen::Ref<Mat<Scalar>>;

	using Real   = double;
	using VecX   = Vec<Real>;
	using MatX   = Mat<Real>;
	using SpMatX = SpMat<Real>;

}  // namespace mathfp::linalg
