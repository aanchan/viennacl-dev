#ifndef VIENNACL_GMRES_HPP_
#define VIENNACL_GMRES_HPP_

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

/** @file gmres.hpp
    @brief Implementations of the generalized minimum residual method are in this file.
*/

#include <vector>
#include <cmath>
#include <limits>
#include "viennacl/forwards.h"
#include "viennacl/tools/tools.hpp"
#include "viennacl/linalg/norm_2.hpp"
#include "viennacl/linalg/prod.hpp"
#include "viennacl/linalg/inner_prod.hpp"
#include "viennacl/traits/clear.hpp"
#include "viennacl/traits/size.hpp"
#include "viennacl/traits/context.hpp"
#include "viennacl/meta/result_of.hpp"

#include "viennacl/linalg/iterative_operations.hpp"
#include "viennacl/vector_proxy.hpp"


namespace viennacl
{
  namespace linalg
  {

    /** @brief A tag for the solver GMRES. Used for supplying solver parameters and for dispatching the solve() function
    */
    class gmres_tag       //generalized minimum residual
    {
      public:
        /** @brief The constructor
        *
        * @param tol            Relative tolerance for the residual (solver quits if ||r|| < tol * ||r_initial||)
        * @param max_iterations The maximum number of iterations (including restarts
        * @param krylov_dim     The maximum dimension of the Krylov space before restart (number of restarts is found by max_iterations / krylov_dim)
        */
        gmres_tag(double tol = 1e-10, unsigned int max_iterations = 300, unsigned int krylov_dim = 20)
         : tol_(tol), iterations_(max_iterations), krylov_dim_(krylov_dim), iters_taken_(0) {}

        /** @brief Returns the relative tolerance */
        double tolerance() const { return tol_; }
        /** @brief Returns the maximum number of iterations */
        unsigned int max_iterations() const { return iterations_; }
        /** @brief Returns the maximum dimension of the Krylov space before restart */
        unsigned int krylov_dim() const { return krylov_dim_; }
        /** @brief Returns the maximum number of GMRES restarts */
        unsigned int max_restarts() const
        {
          unsigned int ret = iterations_ / krylov_dim_;
          if (ret > 0 && (ret * krylov_dim_ == iterations_) )
            return ret - 1;
          return ret;
        }

        /** @brief Return the number of solver iterations: */
        unsigned int iters() const { return iters_taken_; }
        /** @brief Set the number of solver iterations (should only be modified by the solver) */
        void iters(unsigned int i) const { iters_taken_ = i; }

        /** @brief Returns the estimated relative error at the end of the solver run */
        double error() const { return last_error_; }
        /** @brief Sets the estimated relative error at the end of the solver run */
        void error(double e) const { last_error_ = e; }

      private:
        double tol_;
        unsigned int iterations_;
        unsigned int krylov_dim_;

        //return values from solver
        mutable unsigned int iters_taken_;
        mutable double last_error_;
    };

    namespace detail
    {

      template <typename SRC_VECTOR, typename DEST_VECTOR>
      void gmres_copy_helper(SRC_VECTOR const & src, DEST_VECTOR & dest, vcl_size_t len, vcl_size_t start = 0)
      {
        for (vcl_size_t i=0; i<len; ++i)
          dest[start+i] = src[start+i];
      }

      template <typename ScalarType, typename DEST_VECTOR>
      void gmres_copy_helper(viennacl::vector<ScalarType> const & src, DEST_VECTOR & dest, vcl_size_t len, vcl_size_t start = 0)
      {
        typedef typename viennacl::vector<ScalarType>::difference_type   difference_type;
        viennacl::copy( src.begin() + static_cast<difference_type>(start),
                        src.begin() + static_cast<difference_type>(start + len),
                       dest.begin() + static_cast<difference_type>(start));
      }

      /** @brief Computes the householder vector 'hh_vec' which rotates 'input_vec' such that all entries below the j-th entry of 'v' become zero.
        *
        * @param input_vec       The input vector
        * @param hh_vec          The householder vector defining the relection (I - beta * hh_vec * hh_vec^T)
        * @param beta            The coefficient beta in (I - beta  * hh_vec * hh_vec^T)
        * @param mu              The norm of the input vector part relevant for the reflection: norm_2(input_vec[j:size])
        * @param j               Index of the last nonzero index in 'input_vec' after applying the reflection
      */
      template <typename VectorType, typename ScalarType>
      void gmres_setup_householder_vector(VectorType const & input_vec, VectorType & hh_vec, ScalarType & beta, ScalarType & mu, vcl_size_t j)
      {
        ScalarType input_j = input_vec(j);

        // copy entries from input vector to householder vector:
        detail::gmres_copy_helper(input_vec, hh_vec, viennacl::traits::size(hh_vec) - (j+1), j+1);

        ScalarType sigma = viennacl::linalg::norm_2(hh_vec);
        sigma *= sigma;

        if (sigma == 0)
        {
          beta = 0;
          mu = input_j;
        }
        else
        {
          mu = std::sqrt(sigma + input_j*input_j);

          ScalarType hh_vec_0 = (input_j <= 0) ? (input_j - mu) : (-sigma / (input_j + mu));

          beta = ScalarType(2) * hh_vec_0 * hh_vec_0 / (sigma + hh_vec_0 * hh_vec_0);

          //divide hh_vec by its diagonal element hh_vec_0
          hh_vec /= hh_vec_0;
          hh_vec[j] = ScalarType(1);
        }
      }

      // Apply (I - beta h h^T) to x (Householder reflection with Householder vector h)
      template <typename VectorType, typename ScalarType>
      void gmres_householder_reflect(VectorType & x, VectorType const & h, ScalarType beta)
      {
        ScalarType hT_in_x = viennacl::linalg::inner_prod(h, x);
        x -= (beta * hT_in_x) * h;
      }

    }

    /** @brief Implementation of a GMRES solver without preconditioner
    *
    * Following algorithm 2.1 proposed by Walker in "A Simpler GMRES", but uses classical Gram-Schmidt instead of modified Gram-Schmidt for better parallelization.
    *
    * @param matrix     The system matrix
    * @param rhs        The load vector
    * @param tag        Solver configuration tag
    * @param precond    A preconditioner. Precondition operation is done via member function apply()
    * @return The result vector
    */
    template <typename MatrixType, typename ScalarType>
    viennacl::vector<ScalarType> solve(MatrixType const & A, //MatrixType const & A,
                                       viennacl::vector<ScalarType> const & rhs,
                                       gmres_tag const & tag,
                                       viennacl::linalg::no_precond)
    {
      viennacl::vector<ScalarType> residual(rhs);
      viennacl::vector<ScalarType> result = viennacl::zero_vector<ScalarType>(rhs.size(), viennacl::traits::context(rhs));

      viennacl::vector<ScalarType> device_krylov_basis(rhs.internal_size() * tag.krylov_dim(), viennacl::traits::context(rhs)); // not using viennacl::matrix here because of spurious padding in column number
      viennacl::vector<ScalarType> device_buffer_R(tag.krylov_dim()*tag.krylov_dim(), viennacl::traits::context(rhs));
      std::vector<ScalarType>      host_buffer_R(device_buffer_R.size());

      vcl_size_t buffer_size_per_vector = 128;
      vcl_size_t num_buffer_chunks      = 3;
      viennacl::vector<ScalarType> device_inner_prod_buffer = viennacl::zero_vector<ScalarType>(num_buffer_chunks*buffer_size_per_vector, viennacl::traits::context(rhs)); // temporary buffer
      viennacl::vector<ScalarType> device_r_dot_vk_buffer   = viennacl::zero_vector<ScalarType>(buffer_size_per_vector * tag.krylov_dim(), viennacl::traits::context(rhs)); // holds result of first reduction stage for <r, v_k> on device
      viennacl::vector<ScalarType> device_vi_in_vk_buffer   = viennacl::zero_vector<ScalarType>(buffer_size_per_vector * tag.krylov_dim(), viennacl::traits::context(rhs)); // holds <v_i, v_k> for i=0..k-1 on device
      viennacl::vector<ScalarType> device_values_xi_k       = viennacl::zero_vector<ScalarType>(tag.krylov_dim(), viennacl::traits::context(rhs)); // holds values \xi_k = <r, v_k> on device
      std::vector<ScalarType>      host_r_dot_vk_buffer(device_r_dot_vk_buffer.size());
      std::vector<ScalarType>      host_values_xi_k(tag.krylov_dim());
      std::vector<ScalarType>      host_values_eta_k_buffer(tag.krylov_dim());
      std::vector<ScalarType>      host_update_coefficients(tag.krylov_dim());

      ScalarType norm_rhs = viennacl::linalg::norm_2(residual);
      ScalarType rho_0 = norm_rhs;
      ScalarType rho = ScalarType(1);

      tag.iters(0);

      for (unsigned int restart_count = 0; restart_count <= tag.max_restarts(); ++restart_count)
      {
        //
        // prepare restart:
        //
        if (restart_count > 0)
        {
          // compute new residual without introducing a temporary for A*x:
          residual = viennacl::linalg::prod(A, result);
          residual = rhs - residual;

          rho_0 = viennacl::linalg::norm_2(residual);
        }
        residual /= rho_0;
        rho = ScalarType(1);


        //
        // minimize in Krylov basis:
        //
        vcl_size_t k = 0;
        for (k = 0; k < static_cast<vcl_size_t>(tag.krylov_dim()); ++k)
        {
          tag.iters( tag.iters() + 1 ); //increase iteration counter

          if (k == 0)
          {
            // compute v0 = A*r and perform first reduction stage for ||v0||
            viennacl::vector_range<viennacl::vector<ScalarType> > v0(device_krylov_basis, viennacl::range(0, rhs.size()));
            viennacl::linalg::pipelined_gmres_prod(A, residual, v0, device_inner_prod_buffer);

            // Normalize v_1 and compute first reduction stage for <r, v_0> in device_r_dot_vk_buffer:
            viennacl::linalg::pipelined_gmres_normalize_vk(v0, residual,
                                                           device_buffer_R, k*tag.krylov_dim() + k,
                                                           device_inner_prod_buffer, device_r_dot_vk_buffer,
                                                           buffer_size_per_vector, k*buffer_size_per_vector);
          }
          else
          {
            // compute v0 = A*r and perform first reduction stage for ||v0||
            viennacl::vector_range<viennacl::vector<ScalarType> > vk        (device_krylov_basis, viennacl::range( k   *rhs.internal_size(),  k   *rhs.internal_size() + rhs.size()));
            viennacl::vector_range<viennacl::vector<ScalarType> > vk_minus_1(device_krylov_basis, viennacl::range((k-1)*rhs.internal_size(), (k-1)*rhs.internal_size() + rhs.size()));
            viennacl::linalg::pipelined_gmres_prod(A, vk_minus_1, vk, device_inner_prod_buffer);

            //
            // Gram-Schmidt, stage 1: compute first reduction stage of <v_i, v_k>
            //
            viennacl::linalg::pipelined_gmres_gram_schmidt_stage1(device_krylov_basis, rhs.size(), rhs.internal_size(), k, device_vi_in_vk_buffer, buffer_size_per_vector);

            //
            // Gram-Schmidt, stage 2: compute second reduction stage of <v_i, v_k> and use that to compute v_k -= sum_i <v_i, v_k> v_i.
            //                        Store <v_i, v_k> in R-matrix and compute first reduction stage for ||v_k||
            //
            viennacl::linalg::pipelined_gmres_gram_schmidt_stage2(device_krylov_basis, rhs.size(), rhs.internal_size(), k,
                                                                  device_vi_in_vk_buffer,
                                                                  device_buffer_R, tag.krylov_dim(),
                                                                  device_inner_prod_buffer, buffer_size_per_vector);

            //
            // Normalize v_k and compute first reduction stage for <r, v_k> in device_r_dot_vk_buffer:
            //
            viennacl::linalg::pipelined_gmres_normalize_vk(vk, residual,
                                                           device_buffer_R, k*tag.krylov_dim() + k,
                                                           device_inner_prod_buffer, device_r_dot_vk_buffer,
                                                           buffer_size_per_vector, k*buffer_size_per_vector);
          }
        }

        //
        // Run reduction to obtain the values \xi_k = <r, v_k>.
        // Note that unlike Algorithm 2.1 in Walker: "A Simpler GMRES", we do not update the residual
        //
        viennacl::fast_copy(device_r_dot_vk_buffer.begin(), device_r_dot_vk_buffer.end(), host_r_dot_vk_buffer.begin());
        for (std::size_t i=0; i<k; ++i)
        {
          host_values_xi_k[i] = ScalarType(0);
          for (std::size_t j=0; j<buffer_size_per_vector; ++j)
            host_values_xi_k[i] += host_r_dot_vk_buffer[i*buffer_size_per_vector + j];
        }

        // Compute error estimator:
        for (std::size_t i=0; i<k; ++i)
          rho *= std::sin( std::acos(host_values_xi_k[i] / rho) );

        //
        // Bring values in R  back to host:
        //
        viennacl::fast_copy(device_buffer_R.begin(), device_buffer_R.end(), host_buffer_R.begin());

        //
        // Solve minimization problem:
        //
        host_values_eta_k_buffer = host_values_xi_k;

        for (int i=static_cast<int>(k)-1; i>-1; --i)
        {
          for (vcl_size_t j=static_cast<vcl_size_t>(i)+1; j<k; ++j)
            host_values_eta_k_buffer[i] -= host_buffer_R[i + j*k] * host_values_eta_k_buffer[j];

          host_values_eta_k_buffer[i] /= host_buffer_R[i + i*k];
        }

        //
        // Update x += rho * z with z = \eta_0 * residual + sum_{i=0}^{k-1} \eta_{i+1} v_i
        // Note that we have not updated the residual yet, hence this slightly modified as compared to the form given in Algorithm 2.1 in Walker: "A Simpler GMRES"
        //
        for (vcl_size_t i=0; i<k; ++i)
          host_update_coefficients[i] = rho_0 * host_values_eta_k_buffer[i];

        viennacl::fast_copy(host_update_coefficients.begin(), host_update_coefficients.end(), device_values_xi_k.begin()); //reuse device_values_xi_k_buffer here for simplicity

        viennacl::linalg::pipelined_gmres_update_result(result, residual,
                                                        device_krylov_basis, rhs.size(), rhs.internal_size(),
                                                        device_values_xi_k, k);

        tag.error( std::fabs(rho*rho_0 / norm_rhs) );
      }

      return result;
    }

    /** @brief Implementation of the GMRES solver.
    *
    * Following the algorithm proposed by Walker in "A Simpler GMRES"
    *
    * @param matrix     The system matrix
    * @param rhs        The load vector
    * @param tag        Solver configuration tag
    * @param precond    A preconditioner. Precondition operation is done via member function apply()
    * @return The result vector
    */
    template <typename MatrixType, typename VectorType, typename PreconditionerType>
    VectorType solve(const MatrixType & matrix, VectorType const & rhs, gmres_tag const & tag, PreconditionerType const & precond)
    {
      typedef typename viennacl::result_of::value_type<VectorType>::type        ScalarType;
      typedef typename viennacl::result_of::cpu_value_type<ScalarType>::type    CPU_ScalarType;
      unsigned int problem_size = static_cast<unsigned int>(viennacl::traits::size(rhs));
      VectorType result = rhs;
      viennacl::traits::clear(result);

      std::size_t krylov_dim = static_cast<std::size_t>(tag.krylov_dim());
      if (problem_size < krylov_dim)
        krylov_dim = problem_size; //A Krylov space larger than the matrix would lead to seg-faults (mathematically, error is certain to be zero already)

      VectorType res = rhs;
      VectorType v_k_tilde = rhs;
      VectorType v_k_tilde_temp = rhs;

      std::vector< std::vector<CPU_ScalarType> > R(krylov_dim, std::vector<CPU_ScalarType>(tag.krylov_dim()));
      std::vector<CPU_ScalarType> projection_rhs(krylov_dim);

      std::vector<VectorType>      householder_reflectors(krylov_dim, rhs);
      std::vector<CPU_ScalarType>  betas(krylov_dim);

      CPU_ScalarType norm_rhs = viennacl::linalg::norm_2(rhs);

      if (norm_rhs == 0) //solution is zero if RHS norm is zero
        return result;

      tag.iters(0);

      for (unsigned int it = 0; it <= tag.max_restarts(); ++it)
      {
        //
        // (Re-)Initialize residual: r = b - A*x (without temporary for the result of A*x)
        //
        res = rhs;
        res -= viennacl::linalg::prod(matrix, result);  //initial guess zero
        precond.apply(res);

        CPU_ScalarType rho_0 = viennacl::linalg::norm_2(res);

        //
        // Check for premature convergence
        //
        if (rho_0 / norm_rhs < tag.tolerance() ) // norm_rhs is known to be nonzero here
        {
          tag.error(rho_0 / norm_rhs);
          return result;
        }

        //
        // Normalize residual and set 'rho' to 1 as requested in 'A Simpler GMRES' by Walker and Zhou.
        //
        res /= rho_0;
        CPU_ScalarType rho = static_cast<CPU_ScalarType>(1.0);

        //
        // Iterate up until maximal Krylove space dimension is reached:
        //
        std::size_t k = 0;
        for (k = 0; k < krylov_dim; ++k)
        {
          tag.iters( tag.iters() + 1 ); //increase iteration counter

          // prepare storage:
          viennacl::traits::clear(R[k]);
          viennacl::traits::clear(householder_reflectors[k]);

          //compute v_k = A * v_{k-1} via Householder matrices
          if (k == 0)
          {
            v_k_tilde = viennacl::linalg::prod(matrix, res);
            precond.apply(v_k_tilde);
          }
          else
          {
            viennacl::traits::clear(v_k_tilde);
            v_k_tilde[k-1] = CPU_ScalarType(1);

            //Householder rotations, part 1: Compute P_1 * P_2 * ... * P_{k-1} * e_{k-1}
            for (int i = static_cast<int>(k)-1; i > -1; --i)
              detail::gmres_householder_reflect(v_k_tilde, householder_reflectors[i], betas[i]);

            v_k_tilde_temp = viennacl::linalg::prod(matrix, v_k_tilde);
            precond.apply(v_k_tilde_temp);
            v_k_tilde = v_k_tilde_temp;

            //Householder rotations, part 2: Compute P_{k-1} * ... * P_{1} * v_k_tilde
            for (unsigned int i = 0; i < k; ++i)
              detail::gmres_householder_reflect(v_k_tilde, householder_reflectors[i], betas[i]);
          }

          //
          // Compute Householder reflection for v_k_tilde such that all entries below k-th entry are zero:
          //
          CPU_ScalarType rho_k_k = 0;
          detail::gmres_setup_householder_vector(v_k_tilde, householder_reflectors[k], betas[k], rho_k_k, k);

          //
          // copy first k entries from v_k_tilde to R[k] in order to fill k-th column with result of
          // P_k * v_k_tilde = (v[0], ... , v[k-1], norm(v), 0, 0, ...) =: (rho_{1,k}, rho_{2,k}, ..., rho_{k,k}, 0, ..., 0);
          //
          detail::gmres_copy_helper(v_k_tilde, R[k], k);
          R[k][k] = rho_k_k;

          //
          // Update residual: r = P_k r
          // Set zeta_k = r[k] including machine precision considerations: mathematically we have |r[k]| <= rho
          // Set rho *= sin(acos(r[k] / rho))
          //
          detail::gmres_householder_reflect(res, householder_reflectors[k], betas[k]);

          if (res[k] > rho) //machine precision reached
            res[k] = rho;
          if (res[k] < -rho) //machine precision reached
            res[k] = -rho;
          projection_rhs[k] = res[k];

          rho *= std::sin( std::acos(projection_rhs[k] / rho) );

          if (std::fabs(rho * rho_0 / norm_rhs) < tag.tolerance())  // Residual is sufficiently reduced, stop here
          {
            tag.error( std::fabs(rho*rho_0 / norm_rhs) );
            ++k;
            break;
          }
        } // for k

        //
        // Triangular solver stage:
        //

        for (int i=static_cast<int>(k)-1; i>-1; --i)
        {
          for (vcl_size_t j=static_cast<vcl_size_t>(i)+1; j<k; ++j)
            projection_rhs[i] -= R[j][i] * projection_rhs[j];     //R is transposed

          projection_rhs[i] /= R[i][i];
        }

        //
        // Note: 'projection_rhs' now holds the solution (eta_1, ..., eta_k)
        //

        res *= projection_rhs[0];

        if (k > 0)
        {
          for (unsigned int i = 0; i < k-1; ++i)
            res[i] += projection_rhs[i+1];
        }

        //
        // Form z inplace in 'res' by applying P_1 * ... * P_{k}
        //
        for (int i=static_cast<int>(k)-1; i>=0; --i)
          detail::gmres_householder_reflect(res, householder_reflectors[i], betas[i]);

        res *= rho_0;
        result += res;  // x += rho_0 * z    in the paper

        //
        // Check for convergence:
        //
        tag.error(std::fabs(rho*rho_0 / norm_rhs));
        if ( tag.error() < tag.tolerance() )
          return result;
      }

      return result;
    }

    /** @brief Convenience overload of the solve() function using GMRES. Per default, no preconditioner is used
    */
    template <typename MatrixType, typename VectorType>
    VectorType solve(const MatrixType & matrix, VectorType const & rhs, gmres_tag const & tag)
    {
      return solve(matrix, rhs, tag, no_precond());
    }


  }
}

#endif
