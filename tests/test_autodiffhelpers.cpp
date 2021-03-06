/*
  Copyright 2015 SINTEF ICT, Applied Mathematics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#if HAVE_DYNAMIC_BOOST_TEST
#define BOOST_TEST_DYN_LINK
#endif

#define BOOST_TEST_MODULE AutoDiffHelpersTest

#include <opm/autodiff/AutoDiffHelpers.hpp>

#include <boost/test/unit_test.hpp>

using namespace Opm;

namespace {
    template <typename Scalar>
    bool
    operator ==(const Eigen::SparseMatrix<Scalar>& A,
                const Eigen::SparseMatrix<Scalar>& B)
    {
        // Two SparseMatrices are equal if
        //   0) They have the same ordering (enforced by equal types)
        //   1) They have the same outer and inner dimensions
        //   2) They have the same number of non-zero elements
        //   3) They have the same sparsity structure
        //   4) The non-zero elements are equal

        // 1) Outer and inner dimensions
        bool eq =       (A.outerSize() == B.outerSize());
        eq      = eq && (A.innerSize() == B.innerSize());

        // 2) Equal number of non-zero elements
        eq      = eq && (A.nonZeros() == B.nonZeros());

        for (typename Eigen::SparseMatrix<Scalar>::Index
                 k0 = 0, kend = A.outerSize(); eq && (k0 < kend); ++k0) {
            for (typename Eigen::SparseMatrix<Scalar>::InnerIterator
                     iA(A, k0), iB(B, k0); eq && (iA && iB); ++iA, ++iB) {
                // 3) Sparsity structure
                eq = (iA.row() == iB.row()) && (iA.col() == iB.col());

                // 4) Equal non-zero elements
                eq = eq && (iA.value() == iB.value());
            }
        }

        return eq;

        // Note: Investigate implementing this operator as
        // return A.cwiseNotEqual(B).count() == 0;
    }
}





BOOST_AUTO_TEST_CASE(vertcatCollapseJacsTest)
{
    typedef AutoDiffBlock<double> ADB;
    typedef ADB::V V;
    typedef ADB::M M;
    typedef Eigen::SparseMatrix<double> S;

    // We will build a system with the block structure
    // { 2, 0, 1 } (total of three columns) and { 1, 2, 1 } (row) sizes.
    //
    //    value           jacobians
    //      10           1       2   |   3
    //   ----------------------------------
    //      11           0       0   |   0    (empty jacobian)
    //      12           0       0   |   0
    //   ----------------------------------
    //      13           4       5   |   6
    std::vector<ADB> v;
    {
        // First block.
        V val(1);
        val << 10;
        std::vector<M> jacs;
        jacs.reserve(3);
        S s1(1, 2);
        S s2(1, 0);
        S s3(1, 1);
        s1.insert(0, 0) = 1.0;
        s1.insert(0, 1) = 2.0;
        s3.insert(0, 0) = 3.0;
        jacs.push_back(M(s1));
        jacs.push_back(M(s2));
        jacs.push_back(M(s3));
        v.push_back(ADB::function(val, jacs));
    }
    {
        // Second block (with empty jacobian).
        V val(2);
        val << 11, 12;
        v.push_back(ADB::constant(val));
    }
    {
        // Third block.
        V val(1);
        val << 13;
        std::vector<M> jacs(0);
        jacs.reserve(3);
        S s1(1, 2);
        S s2(1, 0);
        S s3(1, 1);
        s1.insert(0, 0) = 4.0;
        s1.insert(0, 1) = 5.0;
        s3.insert(0, 0) = 6.0;
        jacs.push_back(M(s1));
        jacs.push_back(M(s2));
        jacs.push_back(M(s3));
        v.push_back(ADB::function(val, jacs));
    }
    std::vector<int> expected_block_pattern{ 2, 0, 1 };
    BOOST_CHECK(v[0].blockPattern() == expected_block_pattern);

    // Call vertcatCollapseJacs().
    const ADB x = vertcatCollapseJacs(v);

    // Build expected results.
    V expected_val(4);
    expected_val << 10, 11, 12, 13;
    S expected_jac_s(4, 3);
    expected_jac_s.insert(0, 0) = 1.0;
    expected_jac_s.insert(0, 1) = 2.0;
    expected_jac_s.insert(0, 2) = 3.0;
    expected_jac_s.insert(3, 0) = 4.0;
    expected_jac_s.insert(3, 1) = 5.0;
    expected_jac_s.insert(3, 2) = 6.0;
    M expected_jac(expected_jac_s);

    // Compare.
    BOOST_CHECK((x.value() == expected_val).all());
    S derivative;
    x.derivative()[0].toSparse(derivative);
    BOOST_CHECK(derivative == expected_jac_s);
}


BOOST_AUTO_TEST_CASE(supersetTest)
{
    typedef AutoDiffBlock<double> ADB;

    {
        ADB subset = ADB::constant(ADB::V::Ones(3));
        const int full_size = 32;
        std::vector<int> indices = {1, 3, 5};

        AutoDiffBlock<double> n_vals = superset(subset, indices, full_size);
        BOOST_CHECK_EQUAL(n_vals.size(), full_size);
        for (int i=0; i<n_vals.size(); ++i) {
            if (find(indices.begin(), indices.end(), i) != indices.end()) {
                BOOST_CHECK_EQUAL(n_vals.value()[i], 1);
            }
            else {
                BOOST_CHECK_EQUAL(n_vals.value()[i], 0);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(supersetEmptyTest)
{
    typedef AutoDiffBlock<double> ADB;

    {
        ADB subset = ADB::constant(ADB::V::Ones(0));
        const int full_size = 32;
        std::vector<int> indices = {};

        AutoDiffBlock<double> n_vals = superset(subset, indices, full_size);
        BOOST_CHECK_EQUAL(n_vals.size(), full_size);
        for (int i=0; i<n_vals.size(); ++i) {
            BOOST_CHECK_EQUAL(n_vals.value()[i], 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(subsetTest)
{
    typedef AutoDiffBlock<double> ADB;

    {
        ADB superset = ADB::constant(ADB::V::Ones(32));
        std::vector<int> indices = {1, 3, 5};

        AutoDiffBlock<double> n_vals = subset(superset, indices);
        BOOST_CHECK_EQUAL(n_vals.size(), 3);
        for (int i=0; i<n_vals.size(); ++i) {
            if (find(indices.begin(), indices.end(), i) != indices.end()) {
                BOOST_CHECK_EQUAL(n_vals.value()[i], 1);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(subsetEmptyTest)
{
    typedef AutoDiffBlock<double> ADB;

    {
        ADB superset = ADB::constant(ADB::V::Ones(32));
        std::vector<int> indices = {};

        AutoDiffBlock<double> n_vals = subset(superset, indices);
        BOOST_CHECK_EQUAL(n_vals.size(), 0);
    }
}

