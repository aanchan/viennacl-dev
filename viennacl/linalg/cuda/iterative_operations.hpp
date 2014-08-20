#ifndef VIENNACL_LINALG_CUDA_ITERATIVE_OPERATIONS_HPP_
#define VIENNACL_LINALG_CUDA_ITERATIVE_OPERATIONS_HPP_

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

/** @file viennacl/linalg/cuda/iterative_operations.hpp
    @brief Implementations of operations using sparse matrices using CUDA
*/

#include "viennacl/forwards.h"
#include "viennacl/scalar.hpp"
#include "viennacl/vector.hpp"
#include "viennacl/tools/tools.hpp"
#include "viennacl/linalg/cuda/common.hpp"

namespace viennacl
{
  namespace linalg
  {
    namespace cuda
    {

      //
      // CG vector update:
      //

      // cpu scalar
      template<typename T>
      __global__ void pipelined_cg_vector_kernel(T * result,
                                                 T alpha,
                                                 T * p,
                                                 T * r,
                                                 T const * Ap,
                                                 T beta,
                                                 T * inner_prod_buffer,
                                                 unsigned int size)
      {
        T inner_prod_contrib = 0;
        for (unsigned int i = blockDim.x * blockIdx.x + threadIdx.x; i < size; i += gridDim.x * blockDim.x)
        {
          T value_p = p[i];
          T value_r = r[i];

          result[i] += alpha * value_p;
          value_r   -= alpha * Ap[i];
          value_p    = value_r + beta * value_p;

          p[i] = value_p;
          r[i] = value_r;
          inner_prod_contrib += value_r * value_r;
        }

        // parallel reduction in work group
        __shared__ T shared_array[256];
        shared_array[threadIdx.x] = inner_prod_contrib;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
            shared_array[threadIdx.x] += shared_array[threadIdx.x + stride];
        }

        // write results to result array
        if (threadIdx.x == 0)
          inner_prod_buffer[blockIdx.x] = shared_array[0];
      }


      template<typename T>
      void pipelined_cg_vector_update(vector_base<T> & result,
                                      T alpha,
                                      vector_base<T> & p,
                                      vector_base<T> & r,
                                      vector_base<T> const & Ap,
                                      T beta,
                                      vector_base<T> & inner_prod_buffer)
      {
        unsigned int size = result.size();
        pipelined_cg_vector_kernel<<<128, 128>>>(detail::cuda_arg<T>(result),
                                                 alpha,
                                                 detail::cuda_arg<T>(p),
                                                 detail::cuda_arg<T>(r),
                                                 detail::cuda_arg<T>(Ap),
                                                 beta,
                                                 detail::cuda_arg<T>(inner_prod_buffer),
                                                 size);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_cg_vector_kernel");
      }




      //
      // Compressed matrix
      //


      template<typename T>
      __global__ void pipelined_cg_csr_vec_mul_kernel(
                const unsigned int * row_indices,
                const unsigned int * column_indices,
                const T * elements,
                const T * p,
                T * Ap,
                unsigned int size,
                T * inner_prod_buffer,
                unsigned int buffer_size)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp = 0;

        for (unsigned int row  = blockDim.x * blockIdx.x + threadIdx.x;
                          row  < size;
                          row += gridDim.x * blockDim.x)
        {
          T dot_prod = (T)0;
          unsigned int row_end = row_indices[row+1];
          for (unsigned int i = row_indices[row]; i < row_end; ++i)
            dot_prod += elements[i] * p[column_indices[i]];
          Ap[row] = dot_prod;
          inner_prod_ApAp += dot_prod * dot_prod;
          inner_prod_pAp  +=   p[row] * dot_prod;
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
        }
      }




      template<typename T>
      void pipelined_cg_prod(compressed_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        unsigned int size = p.size();
        unsigned int buffer_size_per_vector = static_cast<unsigned int>(inner_prod_buffer.size()) / static_cast<unsigned int>(3);

        pipelined_cg_csr_vec_mul_kernel<<<128, 128>>>(detail::cuda_arg<unsigned int>(A.handle1().cuda_handle()),
                                                      detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                      detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                      detail::cuda_arg<T>(p),
                                                      detail::cuda_arg<T>(Ap),
                                                      size,
                                                      detail::cuda_arg<T>(inner_prod_buffer),
                                                      buffer_size_per_vector);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_cg_csr_vec_mul_kernel");
      }


      //
      // Coordinate Matrix
      //


      template<typename T>
      __global__ void pipelined_cg_coo_vec_mul_kernel(const unsigned int * coords, //(row_index, column_index)
                                                      const T * elements,
                                                      const unsigned int * group_boundaries,
                                                      const T * p,
                                                      T * Ap,
                                                      unsigned int size,
                                                      T * inner_prod_buffer,
                                                      unsigned int buffer_size)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        __shared__ unsigned int shared_rows[128];
        __shared__ T inter_results[128];

        uint2 tmp;
        T val;
        unsigned int group_start = group_boundaries[blockIdx.x];
        unsigned int group_end   = group_boundaries[blockIdx.x + 1];
        unsigned int k_end = (group_end > group_start) ? 1 + (group_end - group_start - 1) / blockDim.x : 0;   // -1 in order to have correct behavior if group_end - group_start == j * blockDim.x

        unsigned int local_index = 0;

        for (unsigned int k = 0; k < k_end; ++k)
        {
          local_index = group_start + k * blockDim.x + threadIdx.x;

          tmp = (local_index < group_end) ? ((const uint2 *)coords)[local_index] : ::make_uint2(0, 0);
          val = (local_index < group_end) ? elements[local_index] * p[tmp.y] : 0;

          //check for carry from previous loop run:
          if (threadIdx.x == 0 && k > 0)
          {
            if (tmp.x == shared_rows[blockDim.x-1])
              val += inter_results[blockDim.x-1];
            else
            {
              T Ap_entry = inter_results[blockDim.x-1];
              Ap[shared_rows[blockDim.x-1]] = Ap_entry;
              inner_prod_ApAp += Ap_entry * Ap_entry;
              inner_prod_pAp  += Ap_entry * p[shared_rows[blockDim.x-1]];
            }
          }

          //segmented parallel reduction begin
          __syncthreads();
          shared_rows[threadIdx.x] = tmp.x;
          inter_results[threadIdx.x] = val;
          T left = 0;
          __syncthreads();

          for (unsigned int stride = 1; stride < blockDim.x; stride *= 2)
          {
            left = (threadIdx.x >= stride && tmp.x == shared_rows[threadIdx.x - stride]) ? inter_results[threadIdx.x - stride] : 0;
            __syncthreads();
            inter_results[threadIdx.x] += left;
            __syncthreads();
          }
          //segmented parallel reduction end

          if (local_index < group_end && threadIdx.x < blockDim.x-1 &&
              shared_rows[threadIdx.x] != shared_rows[threadIdx.x + 1])
          {
            T Ap_entry = inter_results[threadIdx.x];
            Ap[tmp.x] = Ap_entry;
            inner_prod_ApAp += Ap_entry * Ap_entry;
            inner_prod_pAp  += Ap_entry * p[tmp.x];
          }

          __syncthreads();
        } //for k

        if (local_index + 1 == group_end)
        {
          T Ap_entry = inter_results[threadIdx.x];
          Ap[tmp.x] = Ap_entry;
          inner_prod_ApAp += Ap_entry * Ap_entry;
          inner_prod_pAp  += Ap_entry * p[tmp.x];
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
        }

      }


      template<typename T>
      void pipelined_cg_prod(coordinate_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        unsigned int size = p.size();
        unsigned int buffer_size_per_vector = static_cast<unsigned int>(inner_prod_buffer.size()) / static_cast<unsigned int>(3);

        Ap.clear();

        pipelined_cg_coo_vec_mul_kernel<<<128, 128>>>(detail::cuda_arg<unsigned int>(A.handle12().cuda_handle()),
                                                      detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                      detail::cuda_arg<unsigned int>(A.handle3().cuda_handle()),
                                                      detail::cuda_arg<T>(p),
                                                      detail::cuda_arg<T>(Ap),
                                                      size,
                                                      detail::cuda_arg<T>(inner_prod_buffer),
                                                      buffer_size_per_vector);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_cg_coo_vec_mul_kernel");
      }



      //
      // ELL Matrix
      //

      template<typename T>
      __global__ void pipelined_cg_ell_vec_mul_kernel(const unsigned int * coords,
                                                      const T * elements,
                                                      unsigned int internal_row_num,
                                                      unsigned int items_per_row,
                                                      const T * p,
                                                      T * Ap,
                                                      unsigned int size,
                                                      T * inner_prod_buffer,
                                                      unsigned int buffer_size)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        unsigned int glb_id = blockDim.x * blockIdx.x + threadIdx.x;
        unsigned int glb_sz = gridDim.x * blockDim.x;

        for (unsigned int row = glb_id; row < size; row += glb_sz)
        {
          T sum = 0;

          unsigned int offset = row;
          for (unsigned int item_id = 0; item_id < items_per_row; item_id++, offset += internal_row_num)
          {
            T val = elements[offset];
            sum += val ? p[coords[offset]] * val : T(0);
          }

          Ap[row] = sum;
          inner_prod_ApAp += sum * sum;
          inner_prod_pAp  += sum * p[row];
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
        }
      }


      template<typename T>
      void pipelined_cg_prod(ell_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        unsigned int size = p.size();
        unsigned int buffer_size_per_vector = static_cast<unsigned int>(inner_prod_buffer.size()) / static_cast<unsigned int>(3);

        pipelined_cg_ell_vec_mul_kernel<<<256, 128>>>(detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                      detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                      static_cast<unsigned int>(A.internal_size1()),
                                                      static_cast<unsigned int>(A.maxnnz()),
                                                      detail::cuda_arg<T>(p),
                                                      detail::cuda_arg<T>(Ap),
                                                      size,
                                                      detail::cuda_arg<T>(inner_prod_buffer),
                                                      buffer_size_per_vector);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_cg_ell_vec_mul_kernel");
      }


      //
      // SELL-C-\sigma Matrix
      //

      template<typename T>
      __global__ void pipelined_cg_sliced_ell_vec_mul_kernel(const unsigned int * columns_per_block,
                                                             const unsigned int * column_indices,
                                                             const unsigned int * block_start,
                                                             const T * elements,
                                                             const T * p,
                                                             T * Ap,
                                                             unsigned int size,
                                                             T * inner_prod_buffer,
                                                             unsigned int buffer_size)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        unsigned int local_id = threadIdx.x;
        unsigned int local_size = blockDim.x;

        for (unsigned int block_idx = blockIdx.x; block_idx <= size / local_size; block_idx += gridDim.x)
        {
          unsigned int row         = block_idx * local_size + local_id;
          unsigned int offset      = block_start[block_idx];
          unsigned int num_columns = columns_per_block[block_idx];

          T sum = 0;
          for (unsigned int item_id = 0; item_id < num_columns; item_id++)
          {
            unsigned int index = offset + item_id * local_size + local_id;
            T val = elements[index];

            sum += val ? (p[column_indices[index]] * val) : 0;
          }

          if (row < size)
          {
            Ap[row] = sum;
            inner_prod_ApAp += sum * sum;
            inner_prod_pAp  += sum * p[row];
          }
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
        }
      }

      template<typename T>
      void pipelined_cg_prod(sliced_ell_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        unsigned int size = p.size();
        unsigned int buffer_size_per_vector = static_cast<unsigned int>(inner_prod_buffer.size()) / static_cast<unsigned int>(3);

        pipelined_cg_sliced_ell_vec_mul_kernel<<<128, A.rows_per_block()>>>(detail::cuda_arg<unsigned int>(A.handle1().cuda_handle()),
                                                                            detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                                            detail::cuda_arg<unsigned int>(A.handle3().cuda_handle()),
                                                                            detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                                            detail::cuda_arg<T>(p),
                                                                            detail::cuda_arg<T>(Ap),
                                                                            size,
                                                                            detail::cuda_arg<T>(inner_prod_buffer),
                                                                            buffer_size_per_vector);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_cg_sliced_ell_vec_mul_kernel");
      }


      //
      // Hybrid Matrix
      //


      template<typename T>
      __global__ void pipelined_cg_hyb_vec_mul_kernel(const unsigned int * ell_coords,
                                                      const T * ell_elements,
                                                      const unsigned int * csr_rows,
                                                      const unsigned int * csr_cols,
                                                      const T * csr_elements,
                                                      unsigned int internal_row_num,
                                                      unsigned int items_per_row,
                                                      const T * p,
                                                      T * Ap,
                                                      unsigned int size,
                                                      T * inner_prod_buffer,
                                                      unsigned int buffer_size)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        unsigned int glb_id = blockDim.x * blockIdx.x + threadIdx.x;
        unsigned int glb_sz = gridDim.x * blockDim.x;

        for (unsigned int row = glb_id; row < size; row += glb_sz)
        {
          T sum = 0;

          unsigned int offset = row;
          for (unsigned int item_id = 0; item_id < items_per_row; item_id++, offset += internal_row_num)
          {
            T val = ell_elements[offset];

            sum += val ? p[ell_coords[offset]] * val : T(0);
          }

          unsigned int col_begin = csr_rows[row];
          unsigned int col_end   = csr_rows[row + 1];

          for (unsigned int item_id = col_begin; item_id < col_end; item_id++)
          {
            sum += p[csr_cols[item_id]] * csr_elements[item_id];
          }

          Ap[row] = sum;
          inner_prod_ApAp += sum * sum;
          inner_prod_pAp  += sum * p[row];
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
        }
      }



      template<typename T>
      void pipelined_cg_prod(hyb_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        unsigned int size = p.size();
        unsigned int buffer_size_per_vector = static_cast<unsigned int>(inner_prod_buffer.size()) / static_cast<unsigned int>(3);

        pipelined_cg_hyb_vec_mul_kernel<<<256, 128>>>(detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                      detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                      detail::cuda_arg<unsigned int>(A.handle3().cuda_handle()),
                                                      detail::cuda_arg<unsigned int>(A.handle4().cuda_handle()),
                                                      detail::cuda_arg<T>(A.handle5().cuda_handle()),
                                                      static_cast<unsigned int>(A.internal_size1()),
                                                      static_cast<unsigned int>(A.ell_nnz()),
                                                      detail::cuda_arg<T>(p),
                                                      detail::cuda_arg<T>(Ap),
                                                      size,
                                                      detail::cuda_arg<T>(inner_prod_buffer),
                                                      buffer_size_per_vector);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_cg_hyb_vec_mul_kernel");
      }



      /////////////////////////////////////

      template<typename T>
      __global__ void pipelined_bicgstab_update_s_kernel(T * s,
                                                         T const * residual,
                                                         T const * Ap,
                                                         unsigned int size,
                                                         T * inner_prod_buffer,
                                                         unsigned int chunk_size,
                                                         unsigned int chunk_offset)
      {
        T alpha = 0;

        // parallel reduction in work group to compute <r, r0> / <Ap, r0>
        __shared__ T shared_array[256];
        __shared__ T shared_array_Ap_in_r0[256];

        shared_array[threadIdx.x] = inner_prod_buffer[threadIdx.x];
        shared_array_Ap_in_r0[threadIdx.x] = inner_prod_buffer[threadIdx.x + 3 * chunk_size];
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride) {
            shared_array[threadIdx.x]          += shared_array[threadIdx.x + stride];
            shared_array_Ap_in_r0[threadIdx.x] += shared_array_Ap_in_r0[threadIdx.x + stride];
          }
        }

        // compute alpha from reduced values:
        __syncthreads();
        alpha = shared_array[0] / shared_array_Ap_in_r0[0];

        // run vector update and compute first stage of <s, s>
        T inner_prod_contrib = 0;
        for (unsigned int i = blockDim.x * blockIdx.x + threadIdx.x; i < size; i += gridDim.x * blockDim.x)
        {
          T value_s = s[i];

          value_s = residual[i] - alpha * Ap[i];
          inner_prod_contrib += value_s * value_s;

          s[i] = value_s;
        }
        __syncthreads();

        // parallel reduction in work group
        shared_array[threadIdx.x] = inner_prod_contrib;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
            shared_array[threadIdx.x] += shared_array[threadIdx.x + stride];
        }

        // write results to inner_prod_buffer
        if (threadIdx.x == 0)
          inner_prod_buffer[blockIdx.x + chunk_offset] = shared_array[0];
      }

      template<typename T>
      void pipelined_bicgstab_update_s(vector_base<T> & s,
                                       vector_base<T> & r,
                                       vector_base<T> const & Ap,
                                       vector_base<T> & inner_prod_buffer,
                                       vcl_size_t buffer_chunk_size,
                                       vcl_size_t buffer_chunk_offset)
      {
        unsigned int size = static_cast<unsigned int>(s.size());
        unsigned int chunk_size   = static_cast<unsigned int>(buffer_chunk_size);
        unsigned int chunk_offset = static_cast<unsigned int>(buffer_chunk_offset);

        pipelined_bicgstab_update_s_kernel<<<128, 128>>>(detail::cuda_arg<T>(s),
                                                         detail::cuda_arg<T>(r),
                                                         detail::cuda_arg<T>(Ap),
                                                         size,
                                                         detail::cuda_arg<T>(inner_prod_buffer),
                                                         chunk_size,
                                                         chunk_offset);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_update_s_kernel");
      }

      template<typename T>
      __global__ void pipelined_bicgstab_vector_kernel(T * result,
                                                       T alpha,
                                                       T * p,
                                                       T omega,
                                                       T const * s,
                                                       T * residual,
                                                       T const * As,
                                                       T beta,
                                                       T const * Ap,
                                                       T const * r0star,
                                                       T * inner_prod_buffer,
                                                       unsigned int size)
      {
        T inner_prod_r_r0star = 0;
        for (unsigned int i = blockDim.x * blockIdx.x + threadIdx.x; i < size; i += gridDim.x * blockDim.x)
        {
          T value_result = result[i];
          T value_p = p[i];
          T value_s = s[i];
          T value_residual = residual[i];
          T value_As = As[i];
          T value_Ap = Ap[i];
          T value_r0star = r0star[i];

          value_result   += alpha * value_p + omega * value_s;
          value_residual  = value_s - omega * value_As;
          value_p         = value_residual + beta * (value_p - omega * value_Ap);

          result[i]   = value_result;
          residual[i] = value_residual;
          p[i]        = value_p;
          inner_prod_r_r0star += value_residual * value_r0star;
        }

        // parallel reduction in work group
        __shared__ T shared_array[256];
        shared_array[threadIdx.x] = inner_prod_r_r0star;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
            shared_array[threadIdx.x] += shared_array[threadIdx.x + stride];
        }

        // write results to result array
        if (threadIdx.x == 0)
          inner_prod_buffer[blockIdx.x] = shared_array[0];
      }


      template<typename T>
      void pipelined_bicgstab_vector_update(vector_base<T> & result, T alpha, vector_base<T> & p, T omega, vector_base<T> const & s,
                                            vector_base<T> & residual, vector_base<T> const & As,
                                            T beta, vector_base<T> const & Ap,
                                            vector_base<T> const & r0star,
                                            vector_base<T> & inner_prod_buffer, vcl_size_t buffer_chunk_size)
      {
        (void)buffer_chunk_size;
        unsigned int size = static_cast<unsigned int>(result.size());

        pipelined_bicgstab_vector_kernel<<<128, 128>>>(detail::cuda_arg<T>(result),
                                                       alpha,
                                                       detail::cuda_arg<T>(p),
                                                       omega,
                                                       detail::cuda_arg<T>(s),
                                                       detail::cuda_arg<T>(residual),
                                                       detail::cuda_arg<T>(As),
                                                       beta,
                                                       detail::cuda_arg<T>(Ap),
                                                       detail::cuda_arg<T>(r0star),
                                                       detail::cuda_arg<T>(inner_prod_buffer),
                                                       size);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_vector_kernel");
      }



      //
      // Compressed matrix
      //


      template<typename T>
      __global__ void pipelined_bicgstab_csr_vec_mul_kernel(
                const unsigned int * row_indices,
                const unsigned int * column_indices,
                const T * elements,
                const T * p,
                T * Ap,
                const T * r0star,
                unsigned int size,
                T * inner_prod_buffer,
                unsigned int buffer_size,
                unsigned int buffer_offset)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp = 0;
        T inner_prod_r0Ap  = 0;

        for (unsigned int row  = blockDim.x * blockIdx.x + threadIdx.x;
                          row  < size;
                          row += gridDim.x * blockDim.x)
        {
          T dot_prod = (T)0;
          unsigned int row_end = row_indices[row+1];
          for (unsigned int i = row_indices[row]; i < row_end; ++i)
            dot_prod += elements[i] * p[column_indices[i]];
          Ap[row] = dot_prod;
          inner_prod_ApAp += dot_prod * dot_prod;
          inner_prod_pAp  +=   p[row] * dot_prod;
          inner_prod_r0Ap += r0star[row] * dot_prod;
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        __shared__ T shared_array_r0Ap[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        shared_array_r0Ap[threadIdx.x] = inner_prod_r0Ap;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
            shared_array_r0Ap[threadIdx.x] += shared_array_r0Ap[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
          inner_prod_buffer[buffer_offset + blockIdx.x] = shared_array_r0Ap[0];
        }
      }




      template<typename T>
      void pipelined_bicgstab_prod(compressed_matrix<T> const & A,
                                   vector_base<T> const & p,
                                   vector_base<T> & Ap,
                                   vector_base<T> const & r0star,
                                   vector_base<T> & inner_prod_buffer,
                                   vcl_size_t buffer_chunk_size,
                                   vcl_size_t buffer_chunk_offset)
      {
        unsigned int vec_size     = static_cast<unsigned int>(viennacl::traits::size(p));
        unsigned int chunk_size   = static_cast<unsigned int>(buffer_chunk_size);
        unsigned int chunk_offset = static_cast<unsigned int>(buffer_chunk_offset);

        pipelined_bicgstab_csr_vec_mul_kernel<<<128, 128>>>(detail::cuda_arg<unsigned int>(A.handle1().cuda_handle()),
                                                            detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                            detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                            detail::cuda_arg<T>(p),
                                                            detail::cuda_arg<T>(Ap),
                                                            detail::cuda_arg<T>(r0star),
                                                            vec_size,
                                                            detail::cuda_arg<T>(inner_prod_buffer),
                                                            chunk_size,
                                                            chunk_offset);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_csr_vec_mul_kernel");
      }


      //
      // Coordinate Matrix
      //


      template<typename T>
      __global__ void pipelined_bicgstab_coo_vec_mul_kernel(const unsigned int * coords, //(row_index, column_index)
                                                      const T * elements,
                                                      const unsigned int * group_boundaries,
                                                      const T * p,
                                                      T * Ap,
                                                      const T * r0star,
                                                      unsigned int size,
                                                      T * inner_prod_buffer,
                                                      unsigned int buffer_size,
                                                      unsigned int buffer_offset)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        T inner_prod_r0Ap  = 0;
        __shared__ unsigned int shared_rows[128];
        __shared__ T inter_results[128];

        uint2 tmp;
        T val;
        unsigned int group_start = group_boundaries[blockIdx.x];
        unsigned int group_end   = group_boundaries[blockIdx.x + 1];
        unsigned int k_end = (group_end > group_start) ? 1 + (group_end - group_start - 1) / blockDim.x : 0;   // -1 in order to have correct behavior if group_end - group_start == j * blockDim.x

        unsigned int local_index = 0;

        for (unsigned int k = 0; k < k_end; ++k)
        {
          local_index = group_start + k * blockDim.x + threadIdx.x;

          tmp = (local_index < group_end) ? ((const uint2 *)coords)[local_index] : ::make_uint2(0, 0);
          val = (local_index < group_end) ? elements[local_index] * p[tmp.y] : 0;

          //check for carry from previous loop run:
          if (threadIdx.x == 0 && k > 0)
          {
            if (tmp.x == shared_rows[blockDim.x-1])
              val += inter_results[blockDim.x-1];
            else
            {
              T Ap_entry = inter_results[blockDim.x-1];
              Ap[shared_rows[blockDim.x-1]] = Ap_entry;
              inner_prod_ApAp += Ap_entry * Ap_entry;
              inner_prod_pAp  += Ap_entry * p[shared_rows[blockDim.x-1]];
              inner_prod_r0Ap += r0star[shared_rows[blockDim.x-1]] * Ap_entry;
            }
          }

          //segmented parallel reduction begin
          __syncthreads();
          shared_rows[threadIdx.x] = tmp.x;
          inter_results[threadIdx.x] = val;
          T left = 0;
          __syncthreads();

          for (unsigned int stride = 1; stride < blockDim.x; stride *= 2)
          {
            left = (threadIdx.x >= stride && tmp.x == shared_rows[threadIdx.x - stride]) ? inter_results[threadIdx.x - stride] : 0;
            __syncthreads();
            inter_results[threadIdx.x] += left;
            __syncthreads();
          }
          //segmented parallel reduction end

          if (local_index < group_end && threadIdx.x < blockDim.x-1 &&
              shared_rows[threadIdx.x] != shared_rows[threadIdx.x + 1])
          {
            T Ap_entry = inter_results[threadIdx.x];
            Ap[tmp.x] = Ap_entry;
            inner_prod_ApAp += Ap_entry * Ap_entry;
            inner_prod_pAp  += Ap_entry * p[tmp.x];
            inner_prod_r0Ap += r0star[tmp.x] * Ap_entry;
          }

          __syncthreads();
        } //for k

        if (local_index + 1 == group_end)
        {
          T Ap_entry = inter_results[threadIdx.x];
          Ap[tmp.x] = Ap_entry;
          inner_prod_ApAp += Ap_entry * Ap_entry;
          inner_prod_pAp  += Ap_entry * p[tmp.x];
          inner_prod_r0Ap += Ap_entry * r0star[tmp.x];
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        __shared__ T shared_array_r0Ap[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        shared_array_r0Ap[threadIdx.x] = inner_prod_r0Ap;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
            shared_array_r0Ap[threadIdx.x] += shared_array_r0Ap[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
          inner_prod_buffer[buffer_offset + blockIdx.x] = shared_array_r0Ap[0];
        }

      }


      template<typename T>
      void pipelined_bicgstab_prod(coordinate_matrix<T> const & A,
                                   vector_base<T> const & p,
                                   vector_base<T> & Ap,
                                   vector_base<T> const & r0star,
                                   vector_base<T> & inner_prod_buffer,
                                   vcl_size_t buffer_chunk_size,
                                   vcl_size_t buffer_chunk_offset)
      {
        unsigned int vec_size     = static_cast<unsigned int>(viennacl::traits::size(p));
        unsigned int chunk_size   = static_cast<unsigned int>(buffer_chunk_size);
        unsigned int chunk_offset = static_cast<unsigned int>(buffer_chunk_offset);

        Ap.clear();

        pipelined_bicgstab_coo_vec_mul_kernel<<<128, 128>>>(detail::cuda_arg<unsigned int>(A.handle12().cuda_handle()),
                                                            detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                            detail::cuda_arg<unsigned int>(A.handle3().cuda_handle()),
                                                            detail::cuda_arg<T>(p),
                                                            detail::cuda_arg<T>(Ap),
                                                            detail::cuda_arg<T>(r0star),
                                                            vec_size,
                                                            detail::cuda_arg<T>(inner_prod_buffer),
                                                            chunk_size,
                                                            chunk_offset);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_coo_vec_mul_kernel");
      }



      //
      // ELL Matrix
      //

      template<typename T>
      __global__ void pipelined_bicgstab_ell_vec_mul_kernel(const unsigned int * coords,
                                                      const T * elements,
                                                      unsigned int internal_row_num,
                                                      unsigned int items_per_row,
                                                      const T * p,
                                                      T * Ap,
                                                      const T * r0star,
                                                      unsigned int size,
                                                      T * inner_prod_buffer,
                                                      unsigned int buffer_size,
                                                      unsigned int buffer_offset)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        T inner_prod_r0Ap  = 0;
        unsigned int glb_id = blockDim.x * blockIdx.x + threadIdx.x;
        unsigned int glb_sz = gridDim.x * blockDim.x;

        for (unsigned int row = glb_id; row < size; row += glb_sz)
        {
          T sum = 0;

          unsigned int offset = row;
          for (unsigned int item_id = 0; item_id < items_per_row; item_id++, offset += internal_row_num)
          {
            T val = elements[offset];
            sum += val ? p[coords[offset]] * val : T(0);
          }

          Ap[row] = sum;
          inner_prod_ApAp += sum * sum;
          inner_prod_pAp  += sum * p[row];
          inner_prod_r0Ap += sum * r0star[row];
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        __shared__ T shared_array_r0Ap[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        shared_array_r0Ap[threadIdx.x] = inner_prod_r0Ap;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
            shared_array_r0Ap[threadIdx.x] += shared_array_r0Ap[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
          inner_prod_buffer[buffer_offset + blockIdx.x] = shared_array_r0Ap[0];
        }
      }


      template<typename T>
      void pipelined_bicgstab_prod(ell_matrix<T> const & A,
                                   vector_base<T> const & p,
                                   vector_base<T> & Ap,
                                   vector_base<T> const & r0star,
                                   vector_base<T> & inner_prod_buffer,
                                   vcl_size_t buffer_chunk_size,
                                   vcl_size_t buffer_chunk_offset)
      {
        unsigned int vec_size     = static_cast<unsigned int>(viennacl::traits::size(p));
        unsigned int chunk_size   = static_cast<unsigned int>(buffer_chunk_size);
        unsigned int chunk_offset = static_cast<unsigned int>(buffer_chunk_offset);

        pipelined_bicgstab_ell_vec_mul_kernel<<<128, 128>>>(detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                            detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                            static_cast<unsigned int>(A.internal_size1()),
                                                            static_cast<unsigned int>(A.maxnnz()),
                                                            detail::cuda_arg<T>(p),
                                                            detail::cuda_arg<T>(Ap),
                                                            detail::cuda_arg<T>(r0star),
                                                            vec_size,
                                                            detail::cuda_arg<T>(inner_prod_buffer),
                                                            chunk_size,
                                                            chunk_offset);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_ell_vec_mul_kernel");
      }


      //
      // SELL-C-\sigma Matrix
      //

      template<typename T>
      __global__ void pipelined_bicgstab_sliced_ell_vec_mul_kernel(const unsigned int * columns_per_block,
                                                                   const unsigned int * column_indices,
                                                                   const unsigned int * block_start,
                                                                   const T * elements,
                                                                   const T * p,
                                                                   T * Ap,
                                                                   const T * r0star,
                                                                   unsigned int size,
                                                                   T * inner_prod_buffer,
                                                                   unsigned int buffer_size,
                                                                   unsigned int buffer_offset)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        T inner_prod_r0Ap  = 0;
        unsigned int local_id = threadIdx.x;
        unsigned int local_size = blockDim.x;

        for (unsigned int block_idx = blockIdx.x; block_idx <= size / local_size; block_idx += gridDim.x)
        {
          unsigned int row         = block_idx * local_size + local_id;
          unsigned int offset      = block_start[block_idx];
          unsigned int num_columns = columns_per_block[block_idx];

          T sum = 0;
          for (unsigned int item_id = 0; item_id < num_columns; item_id++)
          {
            unsigned int index = offset + item_id * local_size + local_id;
            T val = elements[index];

            sum += val ? (p[column_indices[index]] * val) : 0;
          }

          if (row < size)
          {
            Ap[row] = sum;
            inner_prod_ApAp += sum * sum;
            inner_prod_pAp  += sum * p[row];
            inner_prod_r0Ap += sum * r0star[row];
          }
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        __shared__ T shared_array_r0Ap[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        shared_array_r0Ap[threadIdx.x] = inner_prod_r0Ap;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
            shared_array_r0Ap[threadIdx.x] += shared_array_r0Ap[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
          inner_prod_buffer[buffer_offset + blockIdx.x] = shared_array_r0Ap[0];
        }
      }

      template<typename T>
      void pipelined_bicgstab_prod(sliced_ell_matrix<T> const & A,
                                   vector_base<T> const & p,
                                   vector_base<T> & Ap,
                                   vector_base<T> const & r0star,
                                   vector_base<T> & inner_prod_buffer,
                                   vcl_size_t buffer_chunk_size,
                                   vcl_size_t buffer_chunk_offset)
      {
        unsigned int vec_size     = static_cast<unsigned int>(viennacl::traits::size(p));
        unsigned int chunk_size   = static_cast<unsigned int>(buffer_chunk_size);
        unsigned int chunk_offset = static_cast<unsigned int>(buffer_chunk_offset);

        pipelined_bicgstab_sliced_ell_vec_mul_kernel<<<128, A.rows_per_block()>>>(detail::cuda_arg<unsigned int>(A.handle1().cuda_handle()),
                                                                                  detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                                                  detail::cuda_arg<unsigned int>(A.handle3().cuda_handle()),
                                                                                  detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                                                  detail::cuda_arg<T>(p),
                                                                                  detail::cuda_arg<T>(Ap),
                                                                                  detail::cuda_arg<T>(r0star),
                                                                                  vec_size,
                                                                                  detail::cuda_arg<T>(inner_prod_buffer),
                                                                                  chunk_size,
                                                                                  chunk_offset);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_sliced_ell_vec_mul_kernel");
      }


      //
      // Hybrid Matrix
      //


      template<typename T>
      __global__ void pipelined_bicgstab_hyb_vec_mul_kernel(const unsigned int * ell_coords,
                                                            const T * ell_elements,
                                                            const unsigned int * csr_rows,
                                                            const unsigned int * csr_cols,
                                                            const T * csr_elements,
                                                            unsigned int internal_row_num,
                                                            unsigned int items_per_row,
                                                            const T * p,
                                                            T * Ap,
                                                            const T * r0star,
                                                            unsigned int size,
                                                            T * inner_prod_buffer,
                                                            unsigned int buffer_size,
                                                            unsigned int buffer_offset)
      {
        T inner_prod_ApAp = 0;
        T inner_prod_pAp  = 0;
        T inner_prod_r0Ap  = 0;
        unsigned int glb_id = blockDim.x * blockIdx.x + threadIdx.x;
        unsigned int glb_sz = gridDim.x * blockDim.x;

        for (unsigned int row = glb_id; row < size; row += glb_sz)
        {
          T sum = 0;

          unsigned int offset = row;
          for (unsigned int item_id = 0; item_id < items_per_row; item_id++, offset += internal_row_num)
          {
            T val = ell_elements[offset];

            sum += val ? p[ell_coords[offset]] * val : T(0);
          }

          unsigned int col_begin = csr_rows[row];
          unsigned int col_end   = csr_rows[row + 1];

          for (unsigned int item_id = col_begin; item_id < col_end; item_id++)
          {
            sum += p[csr_cols[item_id]] * csr_elements[item_id];
          }

          Ap[row] = sum;
          inner_prod_ApAp += sum * sum;
          inner_prod_pAp  += sum * p[row];
          inner_prod_r0Ap += sum * r0star[row];
        }

        ////////// parallel reduction in work group
        __shared__ T shared_array_ApAp[256];
        __shared__ T shared_array_pAp[256];
        __shared__ T shared_array_r0Ap[256];
        shared_array_ApAp[threadIdx.x] = inner_prod_ApAp;
        shared_array_pAp[threadIdx.x]  = inner_prod_pAp;
        shared_array_r0Ap[threadIdx.x] = inner_prod_r0Ap;
        for (uint stride=blockDim.x/2; stride > 0; stride /= 2)
        {
          __syncthreads();
          if (threadIdx.x < stride)
          {
            shared_array_ApAp[threadIdx.x] += shared_array_ApAp[threadIdx.x + stride];
            shared_array_pAp[threadIdx.x]  += shared_array_pAp[threadIdx.x + stride];
            shared_array_r0Ap[threadIdx.x] += shared_array_r0Ap[threadIdx.x + stride];
          }
        }

        // write results to result array
        if (threadIdx.x == 0) {
          inner_prod_buffer[  buffer_size + blockIdx.x] = shared_array_ApAp[0];
          inner_prod_buffer[2*buffer_size + blockIdx.x] = shared_array_pAp[0];
          inner_prod_buffer[buffer_offset + blockIdx.x] = shared_array_r0Ap[0];
        }
      }



      template<typename T>
      void pipelined_bicgstab_prod(hyb_matrix<T> const & A,
                                   vector_base<T> const & p,
                                   vector_base<T> & Ap,
                                   vector_base<T> const & r0star,
                                   vector_base<T> & inner_prod_buffer,
                                   vcl_size_t buffer_chunk_size,
                                   vcl_size_t buffer_chunk_offset)
      {
        unsigned int vec_size     = static_cast<unsigned int>(viennacl::traits::size(p));
        unsigned int chunk_size   = static_cast<unsigned int>(buffer_chunk_size);
        unsigned int chunk_offset = static_cast<unsigned int>(buffer_chunk_offset);

        pipelined_bicgstab_hyb_vec_mul_kernel<<<128, 128>>>(detail::cuda_arg<unsigned int>(A.handle2().cuda_handle()),
                                                            detail::cuda_arg<T>(A.handle().cuda_handle()),
                                                            detail::cuda_arg<unsigned int>(A.handle3().cuda_handle()),
                                                            detail::cuda_arg<unsigned int>(A.handle4().cuda_handle()),
                                                            detail::cuda_arg<T>(A.handle5().cuda_handle()),
                                                            static_cast<unsigned int>(A.internal_size1()),
                                                            static_cast<unsigned int>(A.ell_nnz()),
                                                            detail::cuda_arg<T>(p),
                                                            detail::cuda_arg<T>(Ap),
                                                            detail::cuda_arg<T>(r0star),
                                                            vec_size,
                                                            detail::cuda_arg<T>(inner_prod_buffer),
                                                            chunk_size,
                                                            chunk_offset);
        VIENNACL_CUDA_LAST_ERROR_CHECK("pipelined_bicgstab_hyb_vec_mul_kernel");
      }

    } // namespace cuda
  } //namespace linalg
} //namespace viennacl


#endif
