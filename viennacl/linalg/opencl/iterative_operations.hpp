#ifndef VIENNACL_LINALG_OPENCL_ITERATIVE_OPERATIONS_HPP_
#define VIENNACL_LINALG_OPENCL_ITERATIVE_OPERATIONS_HPP_

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

/** @file viennacl/linalg/opencl/iterative_operations.hpp
    @brief  Implementations of specialized kernels for fast iterative solvers using OpenCL
*/

#include <cmath>

#include "viennacl/forwards.h"
#include "viennacl/vector_def.hpp"
#include "viennacl/ocl/device.hpp"
#include "viennacl/ocl/handle.hpp"
#include "viennacl/ocl/kernel.hpp"
#include "viennacl/scalar.hpp"
#include "viennacl/tools/tools.hpp"
#include "viennacl/linalg/opencl/common.hpp"
#include "viennacl/linalg/opencl/kernels/iterative.hpp"
#include "viennacl/meta/predicate.hpp"
#include "viennacl/meta/enable_if.hpp"
#include "viennacl/scheduler/preset.hpp"
#include "viennacl/traits/size.hpp"
#include "viennacl/traits/start.hpp"
#include "viennacl/traits/handle.hpp"
#include "viennacl/traits/stride.hpp"

namespace viennacl
{
  namespace linalg
  {
    namespace opencl
    {

      template<typename T>
      void pipelined_cg_vector_update(vector_base<T> & result,
                                      T alpha,
                                      vector_base<T> & p,
                                      vector_base<T> & r,
                                      vector_base<T> const & Ap,
                                      T beta,
                                      vector_base<T> & inner_prod_buffer)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(result).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "cg_vector_update");
        cl_uint    vec_size = cl_uint(viennacl::traits::size(result));

        viennacl::ocl::enqueue(k(result, alpha, p, r, Ap, beta, inner_prod_buffer, vec_size, viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))));
      }

      template<typename T>
      void pipelined_cg_prod(compressed_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "cg_csr_prod");

        cl_uint vec_size               = cl_uint(viennacl::traits::size(p));
        cl_uint buffer_size_per_vector = cl_uint(inner_prod_buffer.size()) / cl_uint(3);

        k.local_work_size(0, 128);
        k.global_work_size(0, 128*128);
        viennacl::ocl::enqueue(k(A.handle1().opencl_handle(), A.handle2().opencl_handle(), A.handle().opencl_handle(),
                                 p,
                                 Ap,
                                 vec_size,
                                 inner_prod_buffer,
                                 buffer_size_per_vector,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                ));

      }

      template<typename T>
      void pipelined_cg_prod(coordinate_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size               = cl_uint(viennacl::traits::size(p));
        cl_uint buffer_size_per_vector = cl_uint(inner_prod_buffer.size()) / cl_uint(3);

        Ap.clear();

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "cg_coo_prod");
        unsigned int thread_num = 256; //k.local_work_size(0);

        k.local_work_size(0, thread_num);

        k.global_work_size(0, 64 * thread_num);  //64 work groups are hard-coded for now. Gives reasonable performance in most cases

        viennacl::ocl::enqueue(k(A.handle12().opencl_handle(), A.handle().opencl_handle(), A.handle3().opencl_handle(),
                                 p,
                                 Ap,
                                 vec_size,
                                 viennacl::ocl::local_mem(sizeof(cl_uint)*thread_num),
                                 viennacl::ocl::local_mem(sizeof(T)*thread_num),
                                 inner_prod_buffer,
                                 buffer_size_per_vector,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                ));
      }

      template<typename T>
      void pipelined_cg_prod(ell_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size               = cl_uint(viennacl::traits::size(p));
        cl_uint buffer_size_per_vector = cl_uint(inner_prod_buffer.size()) / cl_uint(3);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "cg_ell_prod");

        unsigned int thread_num = 128;
        unsigned int group_num = 256;

        k.local_work_size(0, thread_num);
        k.global_work_size(0, thread_num * group_num);

        viennacl::ocl::enqueue(k(A.handle2().opencl_handle(),
                                 A.handle().opencl_handle(),
                                 cl_uint(A.internal_size1()),
                                 cl_uint(A.maxnnz()),
                                 cl_uint(A.internal_maxnnz()),
                                 viennacl::traits::opencl_handle(p),
                                 viennacl::traits::opencl_handle(Ap),
                                 vec_size,
                                 inner_prod_buffer,
                                 buffer_size_per_vector,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                )
                               );
      }

      template<typename T>
      void pipelined_cg_prod(sliced_ell_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size               = cl_uint(viennacl::traits::size(p));
        cl_uint buffer_size_per_vector = cl_uint(inner_prod_buffer.size()) / cl_uint(3);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "cg_sliced_ell_prod");

        unsigned int thread_num = A.rows_per_block();
        unsigned int group_num = 256;

        k.local_work_size(0, thread_num);
        k.global_work_size(0, thread_num * group_num);

        viennacl::ocl::enqueue(k(A.handle1().opencl_handle(),
                                 A.handle2().opencl_handle(),
                                 A.handle3().opencl_handle(),
                                 A.handle().opencl_handle(),
                                 viennacl::traits::opencl_handle(p),
                                 viennacl::traits::opencl_handle(Ap),
                                 vec_size,
                                 inner_prod_buffer,
                                 buffer_size_per_vector,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                )
                              );
      }


      template<typename T>
      void pipelined_cg_prod(hyb_matrix<T> const & A,
                             vector_base<T> const & p,
                             vector_base<T> & Ap,
                             vector_base<T> & inner_prod_buffer)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size               = cl_uint(viennacl::traits::size(p));
        cl_uint buffer_size_per_vector = cl_uint(inner_prod_buffer.size()) / cl_uint(3);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "cg_hyb_prod");

        unsigned int thread_num = 256;
        unsigned int group_num = 128;

        k.local_work_size(0, thread_num);
        k.global_work_size(0, thread_num * group_num);

        viennacl::ocl::enqueue(k(A.handle2().opencl_handle(),
                                 A.handle().opencl_handle(),
                                 A.handle3().opencl_handle(),
                                 A.handle4().opencl_handle(),
                                 A.handle5().opencl_handle(),
                                 cl_uint(A.internal_size1()),
                                 cl_uint(A.ell_nnz()),
                                 cl_uint(A.internal_ellnnz()),
                                 viennacl::traits::opencl_handle(p),
                                 viennacl::traits::opencl_handle(Ap),
                                 vec_size,
                                 inner_prod_buffer,
                                 buffer_size_per_vector,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                )
                              );
      }


      //////////////////////////// BiCGStab ////////////////////////

      template<typename T>
      void pipelined_bicgstab_update_s(vector_base<T> & s,
                                       vector_base<T> & r,
                                       vector_base<T> const & Ap,
                                       vector_base<T> & inner_prod_buffer,
                                       vcl_size_t buffer_chunk_size,
                                       vcl_size_t buffer_chunk_offset)
      {
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(s).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_update_s");
        cl_uint    vec_size = cl_uint(viennacl::traits::size(s));

        k.local_work_size(0, 128);
        k.global_work_size(0, 128*128);

        cl_uint chunk_size   = cl_uint(buffer_chunk_size);
        cl_uint chunk_offset = cl_uint(buffer_chunk_offset);
        viennacl::ocl::enqueue(k(s, r, Ap,
                                 inner_prod_buffer, chunk_size, chunk_offset, vec_size,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))));
      }

      template<typename T>
      void pipelined_bicgstab_vector_update(vector_base<T> & result, T alpha, vector_base<T> & p, T omega, vector_base<T> const & s,
                                            vector_base<T> & residual, vector_base<T> const & As,
                                            T beta, vector_base<T> const & Ap,
                                            vector_base<T> const & r0star,
                                            vector_base<T> & inner_prod_buffer, vcl_size_t buffer_chunk_size)
      {
        (void)buffer_chunk_size;

        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(s).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_vector_update");
        cl_uint    vec_size = cl_uint(viennacl::traits::size(result));

        k.local_work_size(0, 128);
        k.global_work_size(0, 128*128);
        viennacl::ocl::enqueue(k(result, alpha, p, omega, s,
                                 residual, As,
                                 beta, Ap,
                                 r0star,
                                 inner_prod_buffer,
                                 vec_size, viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                 )
                               );
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
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_csr_prod");

        cl_uint vec_size     = cl_uint(viennacl::traits::size(p));
        cl_uint chunk_size   = cl_uint(buffer_chunk_size);
        cl_uint chunk_offset = cl_uint(buffer_chunk_offset);

        k.local_work_size(0, 128);
        k.global_work_size(0, 128*128);
        viennacl::ocl::enqueue(k(A.handle1().opencl_handle(), A.handle2().opencl_handle(), A.handle().opencl_handle(),
                                 p,
                                 Ap,
                                 r0star,
                                 vec_size,
                                 inner_prod_buffer, chunk_size, chunk_offset,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                ));

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
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size     = cl_uint(viennacl::traits::size(p));
        cl_uint chunk_size   = cl_uint(buffer_chunk_size);
        cl_uint chunk_offset = cl_uint(buffer_chunk_offset);

        Ap.clear();

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_coo_prod");
        unsigned int thread_num = 256; //k.local_work_size(0);

        k.local_work_size(0, thread_num);

        k.global_work_size(0, 64 * thread_num);  //64 work groups are hard-coded for now. Gives reasonable performance in most cases

        viennacl::ocl::enqueue(k(A.handle12().opencl_handle(), A.handle().opencl_handle(), A.handle3().opencl_handle(),
                                 p,
                                 Ap,
                                 r0star,
                                 vec_size,
                                 viennacl::ocl::local_mem(sizeof(cl_uint)*thread_num),
                                 viennacl::ocl::local_mem(sizeof(T)*thread_num),
                                 inner_prod_buffer, chunk_size, chunk_offset,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                ));
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
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size     = cl_uint(viennacl::traits::size(p));
        cl_uint chunk_size   = cl_uint(buffer_chunk_size);
        cl_uint chunk_offset = cl_uint(buffer_chunk_offset);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_ell_prod");

        unsigned int thread_num = 128;
        unsigned int group_num = 128;

        k.local_work_size(0, thread_num);
        k.global_work_size(0, thread_num * group_num);

        viennacl::ocl::enqueue(k(A.handle2().opencl_handle(),
                                 A.handle().opencl_handle(),
                                 cl_uint(A.internal_size1()),
                                 cl_uint(A.maxnnz()),
                                 cl_uint(A.internal_maxnnz()),
                                 viennacl::traits::opencl_handle(p),
                                 viennacl::traits::opencl_handle(Ap),
                                 r0star,
                                 vec_size,
                                 inner_prod_buffer, chunk_size, chunk_offset,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                )
                               );
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
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size     = cl_uint(viennacl::traits::size(p));
        cl_uint chunk_size   = cl_uint(buffer_chunk_size);
        cl_uint chunk_offset = cl_uint(buffer_chunk_offset);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_sliced_ell_prod");

        unsigned int thread_num = A.rows_per_block();
        unsigned int group_num = 128;

        k.local_work_size(0, thread_num);
        k.global_work_size(0, thread_num * group_num);

        viennacl::ocl::enqueue(k(A.handle1().opencl_handle(),
                                 A.handle2().opencl_handle(),
                                 A.handle3().opencl_handle(),
                                 A.handle().opencl_handle(),
                                 viennacl::traits::opencl_handle(p),
                                 viennacl::traits::opencl_handle(Ap),
                                 r0star,
                                 vec_size,
                                 inner_prod_buffer, chunk_size, chunk_offset,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                )
                              );
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
        viennacl::ocl::context & ctx = const_cast<viennacl::ocl::context &>(viennacl::traits::opencl_handle(A).context());
        viennacl::linalg::opencl::kernels::iterative<T>::init(ctx);

        cl_uint vec_size     = cl_uint(viennacl::traits::size(p));
        cl_uint chunk_size   = cl_uint(buffer_chunk_size);
        cl_uint chunk_offset = cl_uint(buffer_chunk_offset);

        viennacl::ocl::kernel & k = ctx.get_kernel(viennacl::linalg::opencl::kernels::iterative<T>::program_name(), "bicgstab_hyb_prod");

        unsigned int thread_num = 256;
        unsigned int group_num = 128;

        k.local_work_size(0, thread_num);
        k.global_work_size(0, thread_num * group_num);

        viennacl::ocl::enqueue(k(A.handle2().opencl_handle(),
                                 A.handle().opencl_handle(),
                                 A.handle3().opencl_handle(),
                                 A.handle4().opencl_handle(),
                                 A.handle5().opencl_handle(),
                                 cl_uint(A.internal_size1()),
                                 cl_uint(A.ell_nnz()),
                                 cl_uint(A.internal_ellnnz()),
                                 viennacl::traits::opencl_handle(p),
                                 viennacl::traits::opencl_handle(Ap),
                                 r0star,
                                 vec_size,
                                 inner_prod_buffer, chunk_size, chunk_offset,
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T)),
                                 viennacl::ocl::local_mem(k.local_work_size() * sizeof(T))
                                )
                              );
      }


    } //namespace opencl
  } //namespace linalg
} //namespace viennacl


#endif