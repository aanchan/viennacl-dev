/* =========================================================================
   Copyright (c) 2010-2014, Institute for Microelectronics,
                            Institute for Analysis and Scientific Computing,
                            TU Wien.
   Portions of this software are copyright by UChicago Argonne, LLC.

                            -----------------
                  ViennaCL - The Vienna Computing Library
                            -----------------

   Project Head:    Karl Rupp                   rupp@iue.tuwien.ac.at

   (A list of authors and contributors can be found in the PDF manual)

   License:         MIT (X11), see file LICENSE in the base directory
============================================================================= */

#define VIENNACL_WITH_UBLAS
//#define NDEBUG
//#define VIENNACL_BUILD_INFO

// We don't need debug mode in UBLAS:
#define BOOST_UBLAS_NDEBUG

#include <utility>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdio.h>
#include <time.h>
//#include "../benchmarks/benchmark-utils.hpp"
#include "viennacl/scalar.hpp"
#include "viennacl/matrix.hpp"
#include "viennacl/linalg/prod.hpp"
#include "viennacl/linalg/norm_1.hpp"
#include "viennacl/linalg/norm_inf.hpp"
#include "viennacl/linalg/norm_frobenius.hpp"
#include "viennacl/matrix_proxy.hpp"
#include "viennacl/vector_proxy.hpp"
#include "viennacl/linalg/norm_1.hpp"
#include "viennacl/linalg/norm_2.hpp"
#include "viennacl/linalg/norm_inf.hpp"
#include "viennacl/linalg/norm_frobenius.hpp"

#include "boost/numeric/ublas/vector.hpp"
#include "boost/numeric/ublas/matrix.hpp"
#include "boost/numeric/ublas/matrix_proxy.hpp"
#include "boost/numeric/ublas/vector_proxy.hpp"
#include "boost/numeric/ublas/io.hpp"

using namespace boost::numeric;

template<typename MatrixType, typename VCLMatrixType>
bool check_for_equality(MatrixType const & ublas_A, VCLMatrixType const & vcl_A, double epsilon)
{
  typedef typename MatrixType::value_type   value_type;

  boost::numeric::ublas::matrix<value_type> vcl_A_cpu(vcl_A.size1(), vcl_A.size2());
  viennacl::backend::finish();  //workaround for a bug in APP SDK 2.7 on Trinity APUs (with Catalyst 12.8)
  viennacl::copy(vcl_A, vcl_A_cpu);

  for (std::size_t i=0; i<ublas_A.size1(); ++i)
  {
    for (std::size_t j=0; j<ublas_A.size2(); ++j)
    {
      if (ublas_A(i,j) != vcl_A_cpu(i,j))
      {
        if ( (std::abs(ublas_A(i,j) - vcl_A_cpu(i,j)) / std::max(std::fabs(ublas_A(i,j)), std::fabs(vcl_A_cpu(i,j))) > epsilon) || (vcl_A_cpu(i,j) != vcl_A_cpu(i,j)) )
        {
          std::cout << "Error at index (" << i << ", " << j << "): " << ublas_A(i,j) << " vs " << vcl_A_cpu(i,j) << std::endl;
          std::cout << std::endl << "TEST failed!" << std::endl;
          return false;
        }
      }
    }
  }

  std::cout << "PASSED!" << std::endl;
  return true;
}




template<typename UBLASMatrixType,
          typename ViennaCLMatrixType1, typename ViennaCLMatrixType2, typename ViennaCLMatrixType3>
int run_test(double epsilon,
             UBLASMatrixType & ublas_A, UBLASMatrixType & ublas_B, UBLASMatrixType & ublas_C,
             ViennaCLMatrixType1 & vcl_A, ViennaCLMatrixType2 & vcl_B, ViennaCLMatrixType3 vcl_C)
{

  typedef typename viennacl::result_of::cpu_value_type<typename ViennaCLMatrixType1::value_type>::type  cpu_value_type;

  cpu_value_type alpha = cpu_value_type(3.1415);
  viennacl::scalar<cpu_value_type>   gpu_alpha = alpha;

  cpu_value_type beta = cpu_value_type(2.7182);
  viennacl::scalar<cpu_value_type>   gpu_beta = beta;


  //
  // Initializer:
  //
  std::cout << "Checking for zero_matrix initializer..." << std::endl;
  ublas_A = ublas::zero_matrix<cpu_value_type>(ublas_A.size1(), ublas_A.size2());
  vcl_A = viennacl::zero_matrix<cpu_value_type>(vcl_A.size1(), vcl_A.size2());
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  std::cout << "Checking for scalar_matrix initializer..." << std::endl;
  ublas_A = ublas::scalar_matrix<cpu_value_type>(ublas_A.size1(), ublas_A.size2(), alpha);
  vcl_A = viennacl::scalar_matrix<cpu_value_type>(vcl_A.size1(), vcl_A.size2(), alpha);
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A =    ublas::scalar_matrix<cpu_value_type>(ublas_A.size1(), ublas_A.size2(), gpu_beta);
  vcl_A   = viennacl::scalar_matrix<cpu_value_type>(  vcl_A.size1(),   vcl_A.size2(), gpu_beta);
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  /*std::cout << "Checking for identity initializer..." << std::endl;
  ublas_A = ublas::identity_matrix<cpu_value_type>(ublas_A.size1());
  vcl_A = viennacl::identity_matrix<cpu_value_type>(vcl_A.size1());
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;*/


  std::cout << std::endl;
  //std::cout << "//" << std::endl;
  //std::cout << "////////// Test: Assignments //////////" << std::endl;
  //std::cout << "//" << std::endl;

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  std::cout << "Testing matrix assignment... ";
  //std::cout << ublas_B(0,0) << " vs. " << vcl_B(0,0) << std::endl;
  ublas_A = ublas_B;
  vcl_A = vcl_B;
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  if (ublas_A.size1() == ublas_A.size2())
  {
    std::cout << "Testing matrix assignment (transposed)... ";
    ublas_A = trans(ublas_B);
    vcl_A   = viennacl::trans(vcl_B);

    if (!check_for_equality(ublas_A, vcl_A, epsilon))
      return EXIT_FAILURE;
  }



  //std::cout << std::endl;
  //std::cout << "//" << std::endl;
  //std::cout << "////////// Test 1: Copy to GPU //////////" << std::endl;
  //std::cout << "//" << std::endl;

  ublas_A = ublas_B;
  viennacl::copy(ublas_B, vcl_A);
  std::cout << "Testing upper left copy to GPU... ";
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;


  ublas_C = ublas_B;
  viennacl::copy(ublas_B, vcl_C);
  std::cout << "Testing lower right copy to GPU... ";
  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;


  //std::cout << std::endl;
  //std::cout << "//" << std::endl;
  //std::cout << "////////// Test 2: Copy from GPU //////////" << std::endl;
  //std::cout << "//" << std::endl;

  std::cout << "Testing upper left copy to A... ";
  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  std::cout << "Testing lower right copy to C... ";
  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;



  //std::cout << "//" << std::endl;
  //std::cout << "////////// Test 3: Addition //////////" << std::endl;
  //std::cout << "//" << std::endl;
  viennacl::copy(ublas_C, vcl_C);

  std::cout << "Inplace add: ";
  ublas_C += ublas_C;
  vcl_C   +=   vcl_C;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  if (ublas_C.size1() == ublas_C.size2())
  {
    std::cout << "Inplace add (transposed): ";
    ublas_C += trans(ublas_C);
    vcl_C   += viennacl::trans(vcl_C);

    if (!check_for_equality(ublas_C, vcl_C, epsilon))
      return EXIT_FAILURE;
  }

  std::cout << "Scaled inplace add: ";
  ublas_C += beta * ublas_A;
  vcl_C   += gpu_beta * vcl_A;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Add: ";
  ublas_C = ublas_A + ublas_B;
  vcl_C   =   vcl_A +   vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Add with flipsign: ";
  ublas_C = - ublas_A + ublas_B;
  vcl_C   = -   vcl_A +   vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;


  std::cout << "Scaled add (left): ";
  ublas_C = long(alpha) * ublas_A + ublas_B;
  vcl_C   = long(alpha) *   vcl_A +   vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ublas_C = float(alpha) * ublas_A + ublas_B;
  vcl_C   = float(alpha) *   vcl_A +   vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ublas_C = double(alpha) * ublas_A + ublas_B;
  vcl_C   = double(alpha) *   vcl_A +   vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled add (left): ";
  vcl_C = gpu_alpha * vcl_A + vcl_B;
  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;


  std::cout << "Scaled add (right): ";
  ublas_C = ublas_A + ublas_B * long(beta);
  vcl_C   =   vcl_A + vcl_B   * long(beta);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ublas_C = ublas_A + ublas_B * float(beta);
  vcl_C   =   vcl_A + vcl_B   * float(beta);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ublas_C = ublas_A + ublas_B * double(beta);
  vcl_C   =   vcl_A + vcl_B   * double(beta);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled add (right): ";
  vcl_C = vcl_A + gpu_beta * vcl_B;
  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled add (right, with division): ";
  ublas_C = ublas_A + ublas_B / long(beta);
  vcl_C   =   vcl_A + vcl_B   / long(beta);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ublas_C = ublas_A + ublas_B / float(beta);
  vcl_C   =   vcl_A + vcl_B   / float(beta);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ublas_C = ublas_A + ublas_B / double(beta);
  vcl_C   =   vcl_A + vcl_B   / double(beta);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;


  std::cout << "Scaled add (both): ";
  ublas_C = alpha * ublas_A + beta * ublas_B;
  vcl_C   = alpha *   vcl_A + beta *   vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled add (both): ";
  vcl_C = gpu_alpha * vcl_A + gpu_beta * vcl_B;
  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  //std::cout << "//" << std::endl;
  //std::cout << "////////// Test 4: Subtraction //////////" << std::endl;
  //std::cout << "//" << std::endl;
  viennacl::copy(ublas_C, vcl_C);

  std::cout << "Inplace sub: ";
  ublas_C -= ublas_B;
  vcl_C -= vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  if (ublas_C.size1() == ublas_C.size2())
  {
    std::cout << "Inplace add (transposed): ";
    ublas_C -= trans(ublas_C);
    vcl_C   -= viennacl::trans(vcl_C);
  }

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled Inplace sub: ";
  ublas_C -= alpha * ublas_B;
  vcl_C -= alpha * vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;




  std::cout << "Sub: ";
  ublas_C = ublas_A - ublas_B;
  vcl_C = vcl_A - vcl_B;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled sub (left): ";
  ublas_B = alpha * ublas_A - ublas_C;
  vcl_B   = alpha *   vcl_A - vcl_C;

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled sub (left): ";
  vcl_B = gpu_alpha * vcl_A - vcl_C;
  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;


  std::cout << "Scaled sub (right): ";
  ublas_B = ublas_A - beta * ublas_C;
  vcl_B   =   vcl_A - vcl_C * beta;

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled sub (right): ";
  vcl_B = vcl_A - vcl_C * gpu_beta;
  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;


  std::cout << "Scaled sub (both): ";
  ublas_B = alpha * ublas_A - beta * ublas_C;
  vcl_B   = alpha * vcl_A - vcl_C * beta;

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  std::cout << "Scaled sub (both): ";
  vcl_B = gpu_alpha * vcl_A - vcl_C * gpu_beta;
  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;


  std::cout << "Unary operator-: ";
  ublas_C = - ublas_A;
  vcl_C   = -   vcl_A;

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;



  //std::cout << "//" << std::endl;
  //std::cout << "////////// Test 5: Scaling //////////" << std::endl;
  //std::cout << "//" << std::endl;
  viennacl::copy(ublas_A, vcl_A);

  std::cout << "Multiplication with CPU scalar: ";
  ublas_A *= long(alpha);
  vcl_A   *= long(alpha);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A *= float(alpha);
  vcl_A   *= float(alpha);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A *= double(alpha);
  vcl_A   *= double(alpha);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  std::cout << "Multiplication with GPU scalar: ";
  ublas_A *= beta;
  vcl_A *= gpu_beta;

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;


  std::cout << "Division with CPU scalar: ";
  ublas_A /= long(alpha);
  vcl_A   /= long(alpha);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A /= float(alpha);
  vcl_A   /= float(alpha);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A /= double(alpha);
  vcl_A   /= double(alpha);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  std::cout << "Division with GPU scalar: ";
  ublas_A /= beta;
  vcl_A /= gpu_beta;

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;



  std::cout << "Testing elementwise multiplication..." << std::endl;
  ublas_B = ublas::scalar_matrix<cpu_value_type>(ublas_B.size1(), ublas_B.size2(), cpu_value_type(1.4142));
  ublas_A = cpu_value_type(3.1415) * ublas_B;
  viennacl::copy(ublas_A, vcl_A);
  viennacl::copy(ublas_B, vcl_B);
  viennacl::copy(ublas_B, vcl_B);
  ublas_A = ublas::element_prod(ublas_A, ublas_B);
  vcl_A = viennacl::linalg::element_prod(vcl_A, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_prod(ublas_A, ublas_B);
  vcl_A += viennacl::linalg::element_prod(vcl_A, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_prod(ublas_A, ublas_B);
  vcl_A -= viennacl::linalg::element_prod(vcl_A, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ///////
  ublas_A = ublas::element_prod(ublas_A + ublas_B, ublas_B);
  vcl_A = viennacl::linalg::element_prod(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_prod(ublas_A + ublas_B, ublas_B);
  vcl_A += viennacl::linalg::element_prod(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_prod(ublas_A + ublas_B, ublas_B);
  vcl_A -= viennacl::linalg::element_prod(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ///////
  ublas_A = ublas::element_prod(ublas_A, ublas_B + ublas_A);
  vcl_A = viennacl::linalg::element_prod(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_prod(ublas_A, ublas_B + ublas_A);
  vcl_A += viennacl::linalg::element_prod(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_prod(ublas_A, ublas_B + ublas_A);
  vcl_A -= viennacl::linalg::element_prod(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ///////
  ublas_A = ublas::element_prod(ublas_A + ublas_B, ublas_B + ublas_A);
  vcl_A = viennacl::linalg::element_prod(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_prod(ublas_A + ublas_B, ublas_B + ublas_A);
  vcl_A += viennacl::linalg::element_prod(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_prod(ublas_A + ublas_B, ublas_B + ublas_A);
  vcl_A -= viennacl::linalg::element_prod(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;


  ublas_B = ublas::scalar_matrix<cpu_value_type>(ublas_B.size1(), ublas_B.size2(), cpu_value_type(1.4142));
  ublas_A = cpu_value_type(3.1415) * ublas_B;
  viennacl::copy(ublas_A, vcl_A);
  viennacl::copy(ublas_B, vcl_B);

  ublas_A = ublas::element_div(ublas_A, ublas_B);
  vcl_A = viennacl::linalg::element_div(vcl_A, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_div(ublas_A, ublas_B);
  vcl_A += viennacl::linalg::element_div(vcl_A, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_div(ublas_A, ublas_B);
  vcl_A -= viennacl::linalg::element_div(vcl_A, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ///////
  ublas_A = ublas::element_div(ublas_A + ublas_B, ublas_B);
  vcl_A = viennacl::linalg::element_div(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_div(ublas_A + ublas_B, ublas_B);
  vcl_A += viennacl::linalg::element_div(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_div(ublas_A + ublas_B, ublas_B);
  vcl_A -= viennacl::linalg::element_div(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ///////
  ublas_A = ublas::element_div(ublas_A, ublas_B + ublas_A);
  vcl_A = viennacl::linalg::element_div(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_div(ublas_A, ublas_B + ublas_A);
  vcl_A += viennacl::linalg::element_div(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_div(ublas_A, ublas_B + ublas_A);
  vcl_A -= viennacl::linalg::element_div(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ///////
  ublas_A = ublas::element_div(ublas_A + ublas_B, ublas_B + ublas_A);
  vcl_A = viennacl::linalg::element_div(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A += ublas::element_div(ublas_A + ublas_B, ublas_B + ublas_A);
  vcl_A += viennacl::linalg::element_div(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  ublas_A -= ublas::element_div(ublas_A + ublas_B, ublas_B + ublas_A);
  vcl_A -= viennacl::linalg::element_div(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_A, vcl_A, epsilon))
    return EXIT_FAILURE;

  // element_pow
  std::cout << "Testing unary element_pow()..." << std::endl;

  ublas_B = ublas::scalar_matrix<cpu_value_type>(ublas_B.size1(), ublas_B.size2(), cpu_value_type(1.4142));
  ublas_A = cpu_value_type(3.1415) * ublas_B;
  viennacl::copy(ublas_A, vcl_A);
  viennacl::copy(ublas_B, vcl_B);

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) = std::pow(ublas_A(i,j), ublas_B(i,j));
  vcl_C = viennacl::linalg::element_pow(vcl_A, vcl_B);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) += std::pow(ublas_A(i,j), ublas_B(i,j));
  vcl_C += viennacl::linalg::element_pow(vcl_A, vcl_B);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) -= std::pow(ublas_A(i,j), ublas_B(i,j));
  vcl_C -= viennacl::linalg::element_pow(vcl_A, vcl_B);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ///////
  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) = std::pow(ublas_A(i,j) + ublas_B(i,j), ublas_B(i,j));
  vcl_C = viennacl::linalg::element_pow(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) += std::pow(ublas_A(i,j) + ublas_B(i,j), ublas_B(i,j));
  vcl_C += viennacl::linalg::element_pow(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) -= std::pow(ublas_A(i,j) + ublas_B(i,j), ublas_B(i,j));
  vcl_C -= viennacl::linalg::element_pow(vcl_A + vcl_B, vcl_B);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ///////
  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) = std::pow(ublas_A(i,j), ublas_B(i,j) + ublas_A(i,j));
  vcl_C = viennacl::linalg::element_pow(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) += std::pow(ublas_A(i,j), ublas_B(i,j) + ublas_A(i,j));
  vcl_C += viennacl::linalg::element_pow(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) -= std::pow(ublas_A(i,j), ublas_B(i,j) + ublas_A(i,j));
  vcl_C -= viennacl::linalg::element_pow(vcl_A, vcl_B + vcl_A);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  ///////
  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) = std::pow(ublas_A(i,j) + ublas_B(i,j), ublas_B(i,j) + ublas_A(i,j));
  vcl_C = viennacl::linalg::element_pow(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) += std::pow(ublas_A(i,j) + ublas_B(i,j), ublas_B(i,j) + ublas_A(i,j));
  vcl_C += viennacl::linalg::element_pow(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;

  for (std::size_t i=0; i<ublas_C.size1(); i++)
    for (std::size_t j=0; j<ublas_C.size2(); ++j)
      ublas_C(i,j) -= std::pow(ublas_A(i,j) + ublas_B(i,j), ublas_B(i,j) + ublas_A(i,j));
  vcl_C -= viennacl::linalg::element_pow(vcl_A + vcl_B, vcl_B + vcl_A);

  if (!check_for_equality(ublas_C, vcl_C, epsilon))
    return EXIT_FAILURE;


  std::cout << "Testing unary elementwise operations..." << std::endl;

#define GENERATE_UNARY_OP_TEST(FUNCNAME) \
  ublas_B = ublas::scalar_matrix<cpu_value_type>(ublas_B.size1(), ublas_B.size2(), cpu_value_type(1.4142)); \
  ublas_A = cpu_value_type(3.1415) * ublas_B; \
  ublas_C = cpu_value_type(2.7172) * ublas_A; \
  viennacl::copy(ublas_A, vcl_A); \
  viennacl::copy(ublas_B, vcl_B); \
  viennacl::copy(ublas_C, vcl_C); \
  viennacl::copy(ublas_B, vcl_B); \
  \
  for (std::size_t i=0; i<ublas_C.size1(); ++i) \
    for (std::size_t j=0; j<ublas_C.size2(); ++j) \
      ublas_C(i,j) = std::FUNCNAME(ublas_A(i,j)); \
  vcl_C = viennacl::linalg::element_##FUNCNAME(vcl_A); \
 \
  if (!check_for_equality(ublas_C, vcl_C, epsilon)) \
  { \
    std::cout << "Failure at C = " << #FUNCNAME << "(A)" << std::endl; \
    return EXIT_FAILURE; \
  } \
 \
  for (std::size_t i=0; i<ublas_C.size1(); ++i) \
    for (std::size_t j=0; j<ublas_C.size2(); ++j) \
      ublas_C(i,j) = std::FUNCNAME(ublas_A(i,j) + ublas_B(i,j)); \
  vcl_C = viennacl::linalg::element_##FUNCNAME(vcl_A + vcl_B); \
 \
  if (!check_for_equality(ublas_C, vcl_C, epsilon)) \
  { \
    std::cout << "Failure at C = " << #FUNCNAME << "(A + B)" << std::endl; \
    return EXIT_FAILURE; \
  } \
 \
  for (std::size_t i=0; i<ublas_C.size1(); ++i) \
    for (std::size_t j=0; j<ublas_C.size2(); ++j) \
      ublas_C(i,j) += std::FUNCNAME(ublas_A(i,j)); \
  vcl_C += viennacl::linalg::element_##FUNCNAME(vcl_A); \
 \
  if (!check_for_equality(ublas_C, vcl_C, epsilon)) \
  { \
    std::cout << "Failure at C += " << #FUNCNAME << "(A)" << std::endl; \
    return EXIT_FAILURE; \
  } \
 \
  for (std::size_t i=0; i<ublas_C.size1(); ++i) \
    for (std::size_t j=0; j<ublas_C.size2(); ++j) \
      ublas_C(i,j) += std::FUNCNAME(ublas_A(i,j) + ublas_B(i,j)); \
  vcl_C += viennacl::linalg::element_##FUNCNAME(vcl_A + vcl_B); \
 \
  if (!check_for_equality(ublas_C, vcl_C, epsilon)) \
  { \
    std::cout << "Failure at C += " << #FUNCNAME << "(A + B)" << std::endl; \
    return EXIT_FAILURE; \
  } \
 \
  for (std::size_t i=0; i<ublas_C.size1(); ++i) \
    for (std::size_t j=0; j<ublas_C.size2(); ++j) \
      ublas_C(i,j) -= std::FUNCNAME(ublas_A(i,j)); \
  vcl_C -= viennacl::linalg::element_##FUNCNAME(vcl_A); \
 \
  if (!check_for_equality(ublas_C, vcl_C, epsilon)) \
  { \
    std::cout << "Failure at C -= " << #FUNCNAME << "(A)" << std::endl; \
    return EXIT_FAILURE; \
  } \
 \
  for (std::size_t i=0; i<ublas_C.size1(); ++i) \
    for (std::size_t j=0; j<ublas_C.size2(); ++j) \
      ublas_C(i,j) -= std::FUNCNAME(ublas_A(i,j) + ublas_B(i,j)); \
  vcl_C -= viennacl::linalg::element_##FUNCNAME(vcl_A + vcl_B); \
 \
  if (!check_for_equality(ublas_C, vcl_C, epsilon)) \
  { \
    std::cout << "Failure at C -= " << #FUNCNAME << "(A + B)" << std::endl; \
    return EXIT_FAILURE; \
  } \
 \

  GENERATE_UNARY_OP_TEST(cos);
  GENERATE_UNARY_OP_TEST(cosh);
  GENERATE_UNARY_OP_TEST(exp);
  GENERATE_UNARY_OP_TEST(floor);
  GENERATE_UNARY_OP_TEST(fabs);
  GENERATE_UNARY_OP_TEST(log);
  GENERATE_UNARY_OP_TEST(log10);
  GENERATE_UNARY_OP_TEST(sin);
  GENERATE_UNARY_OP_TEST(sinh);
  GENERATE_UNARY_OP_TEST(fabs);
  GENERATE_UNARY_OP_TEST(sqrt);
  GENERATE_UNARY_OP_TEST(tan);
  GENERATE_UNARY_OP_TEST(tanh);

  std::cout << "Complicated expressions: ";
  //std::cout << "ublas_A: " << ublas_A << std::endl;
  //std::cout << "ublas_B: " << ublas_B << std::endl;
  //std::cout << "ublas_C: " << ublas_C << std::endl;
  ublas_B +=     alpha * (- ublas_A - beta * ublas_C + ublas_A);
  vcl_B   += gpu_alpha * (-   vcl_A - vcl_C * beta   +   vcl_A);

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  ublas_B += (- ublas_A - beta * ublas_C + ublas_A * beta) / gpu_alpha;
  vcl_B   += (-   vcl_A - vcl_C * beta + gpu_beta * vcl_A) / gpu_alpha;

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;


  ublas_B -=     alpha * (- ublas_A - beta * ublas_C - ublas_A);
  vcl_B   -= gpu_alpha * (-   vcl_A - vcl_C * beta - vcl_A);

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  ublas_B -= (- ublas_A - beta * ublas_C - ublas_A * beta) / alpha;
  vcl_B   -= (-   vcl_A - vcl_C * beta - gpu_beta * vcl_A) / gpu_alpha;

  if (!check_for_equality(ublas_B, vcl_B, epsilon))
    return EXIT_FAILURE;

  std::cout << std::endl;
  std::cout << "----------------------------------------------" << std::endl;
  std::cout << std::endl;


  return EXIT_SUCCESS;
}




template<typename T, typename ScalarType>
int run_test(double epsilon)
{
    //typedef float               ScalarType;
    typedef boost::numeric::ublas::matrix<ScalarType>       MatrixType;

    typedef viennacl::matrix<ScalarType, T>    VCLMatrixType;

    std::size_t dim_rows = 131;
    std::size_t dim_cols = 33;
    //std::size_t dim_rows = 5;
    //std::size_t dim_cols = 3;

    //setup ublas objects:
    MatrixType ublas_A(dim_rows, dim_cols);
    MatrixType ublas_B(dim_rows, dim_cols);
    MatrixType ublas_C(dim_rows, dim_cols);

    for (std::size_t i=0; i<ublas_A.size1(); ++i)
      for (std::size_t j=0; j<ublas_A.size2(); ++j)
      {
        ublas_A(i,j) = ScalarType((i+2) + (j+1)*(i+2));
        ublas_B(i,j) = ScalarType((j+2) + (j+1)*(j+2));
        ublas_C(i,j) = ScalarType((i+1) + (i+1)*(i+2));
      }

    MatrixType ublas_A_large(4 * dim_rows, 4 * dim_cols);
    for (std::size_t i=0; i<ublas_A_large.size1(); ++i)
      for (std::size_t j=0; j<ublas_A_large.size2(); ++j)
        ublas_A_large(i,j) = ScalarType(i * ublas_A_large.size2() + j);

    //Setup ViennaCL objects
    VCLMatrixType vcl_A_full(4 * dim_rows, 4 * dim_cols);
    VCLMatrixType vcl_B_full(4 * dim_rows, 4 * dim_cols);
    VCLMatrixType vcl_C_full(4 * dim_rows, 4 * dim_cols);

    viennacl::copy(ublas_A_large, vcl_A_full);
    viennacl::copy(ublas_A_large, vcl_B_full);
    viennacl::copy(ublas_A_large, vcl_C_full);

    //
    // Create A
    //
    VCLMatrixType vcl_A(dim_rows, dim_cols);

    viennacl::range vcl_A_r1(2 * dim_rows, 3 * dim_rows);
    viennacl::range vcl_A_r2(dim_cols, 2 * dim_cols);
    viennacl::matrix_range<VCLMatrixType>   vcl_range_A(vcl_A_full, vcl_A_r1, vcl_A_r2);

    viennacl::slice vcl_A_s1(2, 3, dim_rows);
    viennacl::slice vcl_A_s2(2 * dim_cols, 2, dim_cols);
    viennacl::matrix_slice<VCLMatrixType>   vcl_slice_A(vcl_A_full, vcl_A_s1, vcl_A_s2);


    //
    // Create B
    //
    VCLMatrixType vcl_B(dim_rows, dim_cols);

    viennacl::range vcl_B_r1(dim_rows, 2 * dim_rows);
    viennacl::range vcl_B_r2(2 * dim_cols, 3 * dim_cols);
    viennacl::matrix_range<VCLMatrixType>   vcl_range_B(vcl_B_full, vcl_B_r1, vcl_B_r2);

    viennacl::slice vcl_B_s1(2 * dim_rows, 2, dim_rows);
    viennacl::slice vcl_B_s2(dim_cols, 3, dim_cols);
    viennacl::matrix_slice<VCLMatrixType>   vcl_slice_B(vcl_B_full, vcl_B_s1, vcl_B_s2);


    //
    // Create C
    //
    VCLMatrixType vcl_C(dim_rows, dim_cols);

    viennacl::range vcl_C_r1(2 * dim_rows, 3 * dim_rows);
    viennacl::range vcl_C_r2(3 * dim_cols, 4 * dim_cols);
    viennacl::matrix_range<VCLMatrixType>   vcl_range_C(vcl_C_full, vcl_C_r1, vcl_C_r2);

    viennacl::slice vcl_C_s1(dim_rows, 2, dim_rows);
    viennacl::slice vcl_C_s2(0, 3, dim_cols);
    viennacl::matrix_slice<VCLMatrixType>   vcl_slice_C(vcl_C_full, vcl_C_s1, vcl_C_s2);

    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_A, vcl_slice_A);

    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_B, vcl_slice_B);

    viennacl::copy(ublas_C, vcl_C);
    viennacl::copy(ublas_C, vcl_range_C);
    viennacl::copy(ublas_C, vcl_slice_C);


    std::cout << std::endl;
    std::cout << "//" << std::endl;
    std::cout << "////////// Test: Copy CTOR //////////" << std::endl;
    std::cout << "//" << std::endl;

    {
      std::cout << "Testing matrix created from range... ";
      VCLMatrixType vcl_temp = vcl_range_A;
      if (check_for_equality(ublas_A, vcl_temp, epsilon))
        std::cout << "PASSED!" << std::endl;
      else
      {
        std::cout << "ublas_A: " << ublas_A << std::endl;
        std::cout << "vcl_temp: " << vcl_temp << std::endl;
        std::cout << "vcl_range_A: " << vcl_range_A << std::endl;
        std::cout << "vcl_A: " << vcl_A << std::endl;
        std::cout << std::endl << "TEST failed!" << std::endl;
        return EXIT_FAILURE;
      }

      std::cout << "Testing matrix created from slice... ";
      VCLMatrixType vcl_temp2 = vcl_range_B;
      if (check_for_equality(ublas_B, vcl_temp2, epsilon))
        std::cout << "PASSED!" << std::endl;
      else
      {
        std::cout << std::endl << "TEST failed!" << std::endl;
        return EXIT_FAILURE;
      }
    }

    std::cout << "//" << std::endl;
    std::cout << "////////// Test: Initializer for matrix type //////////" << std::endl;
    std::cout << "//" << std::endl;

    {
      ublas::matrix<ScalarType> ublas_dummy1 = ublas::identity_matrix<ScalarType>(ublas_A.size1());
      ublas::matrix<ScalarType> ublas_dummy2 = ublas::scalar_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1(), 3.0);
      ublas::matrix<ScalarType> ublas_dummy3 = ublas::zero_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1());

      viennacl::matrix<ScalarType> vcl_dummy1 = viennacl::identity_matrix<ScalarType>(ublas_A.size1());
      viennacl::matrix<ScalarType> vcl_dummy2 = viennacl::scalar_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1(), 3.0);
      viennacl::matrix<ScalarType> vcl_dummy3 = viennacl::zero_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1());

      std::cout << "Testing initializer CTOR... ";
      if (   check_for_equality(ublas_dummy1, vcl_dummy1, epsilon)
          && check_for_equality(ublas_dummy2, vcl_dummy2, epsilon)
          && check_for_equality(ublas_dummy3, vcl_dummy3, epsilon)
         )
        std::cout << "PASSED!" << std::endl;
      else
      {
        std::cout << std::endl << "TEST failed!" << std::endl;
        return EXIT_FAILURE;
      }

      ublas_dummy1 = ublas::zero_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1());
      ublas_dummy2 = ublas::identity_matrix<ScalarType>(ublas_A.size1());
      ublas_dummy3 = ublas::scalar_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1(), 3.0);

      vcl_dummy1 = viennacl::zero_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1());
      vcl_dummy2 = viennacl::identity_matrix<ScalarType>(ublas_A.size1());
      vcl_dummy3 = viennacl::scalar_matrix<ScalarType>(ublas_A.size1(), ublas_A.size1(), 3.0);

      std::cout << "Testing initializer assignment... ";
      if (   check_for_equality(ublas_dummy1, vcl_dummy1, epsilon)
          && check_for_equality(ublas_dummy2, vcl_dummy2, epsilon)
          && check_for_equality(ublas_dummy3, vcl_dummy3, epsilon)
         )
        std::cout << "PASSED!" << std::endl;
      else
      {
        std::cout << std::endl << "TEST failed!" << std::endl;
        return EXIT_FAILURE;
      }
    }

    std::cout << "//" << std::endl;
    std::cout << "////////// Test: Norms //////////" << std::endl;
    std::cout << "//" << std::endl;

    /*ScalarType ublas_norm_1 = viennacl::linalg::norm_1(ublas_C);
    ScalarType   vcl_norm_1 = viennacl::linalg::norm_1(vcl_C);
    if ( std::fabs(ublas_norm_1 - vcl_norm_1) / ublas_norm_1 > epsilon)
    {
      std::cerr << "Failure at norm_1(): " << std::fabs(ublas_norm_1 - vcl_norm_1) / ublas_norm_1  << std::endl;
      return EXIT_FAILURE;
    }

    ScalarType ublas_norm_inf = ublas::norm_inf(ublas_C);
    ScalarType   vcl_norm_inf = viennacl::linalg::norm_inf(vcl_C);
    if ( std::fabs(ublas_norm_inf - vcl_norm_inf) / ublas_norm_inf > epsilon)
    {
      std::cerr << "Failure at norm_inf(): " << std::fabs(ublas_norm_inf - vcl_norm_inf) / ublas_norm_inf << std::endl;
      return EXIT_FAILURE;
    }*/

    ScalarType ublas_norm_frobenius = viennacl::linalg::norm_frobenius(ublas_C);
    ScalarType   vcl_norm_frobenius = viennacl::linalg::norm_frobenius(vcl_C);
    if ( std::fabs(ublas_norm_frobenius - vcl_norm_frobenius) / ublas_norm_frobenius > epsilon)
    {
      std::cerr << "Failure at norm_frobenius()" << std::endl;
      return EXIT_FAILURE;
    }

    viennacl::scalar<ScalarType> device_ublas_norm_frobenius = viennacl::linalg::norm_frobenius(ublas_C);
    viennacl::scalar<ScalarType>   device_vcl_norm_frobenius = viennacl::linalg::norm_frobenius(vcl_C);
    if ( std::fabs(device_ublas_norm_frobenius - device_vcl_norm_frobenius) / device_ublas_norm_frobenius > epsilon)
    {
      std::cerr << "Failure at norm_frobenius()" << std::endl;
      return EXIT_FAILURE;
    }

    vcl_norm_frobenius = viennacl::linalg::norm_frobenius(vcl_range_C);
    if ( std::fabs(ublas_norm_frobenius - vcl_norm_frobenius) / ublas_norm_frobenius > epsilon)
    {
      std::cerr << "Failure at norm_frobenius() with range" << std::endl;
      return EXIT_FAILURE;
    }

    device_vcl_norm_frobenius = viennacl::linalg::norm_frobenius(vcl_range_C);
    if ( std::fabs(device_ublas_norm_frobenius - device_vcl_norm_frobenius) / device_ublas_norm_frobenius > epsilon)
    {
      std::cerr << "Failure at norm_frobenius() with range" << std::endl;
      return EXIT_FAILURE;
    }

    vcl_norm_frobenius = viennacl::linalg::norm_frobenius(vcl_slice_C);
    if ( std::fabs(ublas_norm_frobenius - vcl_norm_frobenius) / ublas_norm_frobenius > epsilon)
    {
      std::cerr << "Failure at norm_frobenius() with slice" << std::endl;
      return EXIT_FAILURE;
    }

    device_vcl_norm_frobenius = viennacl::linalg::norm_frobenius(vcl_slice_C);
    if ( std::fabs(device_ublas_norm_frobenius - device_vcl_norm_frobenius) / device_ublas_norm_frobenius > epsilon)
    {
      std::cerr << "Failure at norm_frobenius() with slice" << std::endl;
      return EXIT_FAILURE;
    }

    std::cout << "PASSED!" << std::endl;


    //
    // run operation tests:
    //

    std::cout << "//" << std::endl;
    std::cout << "////////// Test: Operations //////////" << std::endl;
    std::cout << "//" << std::endl;


    /////// A=matrix:
    std::cout << "Testing A=matrix, B=matrix, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=matrix, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=matrix, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=range, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_range_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=range, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_range_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=range, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_range_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }


    std::cout << "Testing A=matrix, B=slice, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_slice_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=slice, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_slice_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=matrix, B=slice, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_A, vcl_slice_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }



    /////// A=range:
    std::cout << "Testing A=range, B=matrix, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=range, B=matrix, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=range, B=matrix, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }



    std::cout << "Testing A=range, B=range, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_range_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=range, B=range, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_range_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=range, B=range, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_range_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }



    std::cout << "Testing A=range, B=slice, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_slice_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=range, B=slice, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_slice_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=range, B=slice, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_range_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_range_A, vcl_slice_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }


    /////// A=slice:
    std::cout << "Testing A=slice, B=matrix, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=slice, B=matrix, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=slice, B=matrix, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }



    std::cout << "Testing A=slice, B=range, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_range_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=slice, B=range, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_range_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=slice, B=range, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_range_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_range_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }



    std::cout << "Testing A=slice, B=slice, C=matrix ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_slice_B, vcl_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=slice, B=slice, C=range ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_range_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_slice_B, vcl_range_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

    std::cout << "Testing A=slice, B=slice, C=slice ..." << std::endl;
    viennacl::copy(ublas_A, vcl_slice_A);
    viennacl::copy(ublas_B, vcl_slice_B);
    viennacl::copy(ublas_C, vcl_slice_C);
    if (run_test(epsilon,
                 ublas_A, ublas_B, ublas_C,
                 vcl_slice_A, vcl_slice_B, vcl_slice_C) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }


    return EXIT_SUCCESS;
}


