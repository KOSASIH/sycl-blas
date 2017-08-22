#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <operations/blas1_trees.hpp>
#include <interface/blas1_interface_sycl.hpp>

using namespace cl::sycl;
using namespace blas;

#define COMPUTECPP_EXPORT
#include <SYCL/codeplay/apis.h>

#define DEF_NUM_ELEM 1200
#define DEF_STRIDE 1
#define ERROR_ALLOWED 1.0E-6
// #define SHOW_VALUES   1
#define SHOW_TIMES 1  // If it exists, the code prints the execution time
                      // The ... should be changed by the corresponding routine
#define NUMBER_REPEATS 6  // Number of times the computations are made
// If it is greater than 1, the compile time is not considered

#define BASETYPE double

// #define GPU 1

#ifdef GPU
#define LOCALSIZE 256
#define INTERLOOP 1
#else
#define LOCALSIZE 8
#define INTERLOOP 1
#endif

#define REDUCE_SCRATCH 1

#define ONE_SCRATCH 1

#define FUSION_ADDS      1
#define FUSION_TWO_ADDS  1
#define FUSION_FOUR_ADDS 1

#define TEST_COPY 1
#define TEST_AXPY 1
#define TEST_ADD  1

#define NUM_JUMPS_INIT_BLOCK  16
// #define NUM_JUMPS_INIT_BLOCK  16 / NUM_LOCAL_ADDS

std::pair<unsigned, unsigned> get_reduction_params(size_t N) {
  /*
  The localsize should be the size of the multiprocessor.
  nWG determines the number of reduction steps.
  Experimentally, we concluded that two steps were the best option in Kepler.
  For these reasons, we fixed nWG = 2 * localsize.
  In other GPUs, the conclusion could be different.
  You can test two options:
    * nWG = (N + localsize - 1) / localsize
    * nWG = (N + 2 * localsize - 1) / (2 * localsize)
  */
  unsigned localSize = LOCALSIZE;

//  unsigned nWg = (N + localSize - 1) / localSize;
//  unsigned nWg = (N + 2 * localSize - 1) / (2 * localSize);
//  unsigned nWg = (N < (32*localSize))? 1: 2 * localSize;
//  unsigned nWg = (N < (localSize))? 1: localSize;
//  unsigned nWg = (N <= (NUM_LOCAL_ADDS * NUM_JUMPS_INIT_BLOCK * localSize))? 1: NUM_LOCAL_ADDS * localSize;
// unsigned nWg = LOCAL_REDUCTIONS * localSize;
// unsigned nWg = (N + LOCAL_REDUCTIONS * localSize - 1) / (LOCAL_REDUCTIONS *
// localSize);

#ifdef GPU
//  unsigned nWg = (N + NUM_LOCAL_ADDS * localSize - 1) / (NUM_LOCAL_ADDS * localSize);
  unsigned nWg = (N <= (NUM_JUMPS_INIT_BLOCK * localSize))? 1: NUM_LOCAL_ADDS * localSize;
#else
  unsigned nWg = 1;
#endif

  return std::pair<unsigned, unsigned>(localSize, nWg);
}

// #########################
// #include <adds.hpp>
// #########################
template <typename ExecutorType, typename T, typename ContainerT>
void _one_add(Executor<ExecutorType> ex, int _N, vector_view<T, ContainerT> _vx,
              int _incx, vector_view<T, ContainerT> _rs,
              vector_view<T, ContainerT> _sc) {
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, _N);
  auto my_rs = vector_view<T, ContainerT>(_rs, _rs.getDisp(), 1, 1);
  auto my_sc = vector_view<T, ContainerT>(_sc, _sc.getDisp(), 1, 1);
#ifdef VERBOSE
  my_vx.printH("VX");
  my_rs.printH("VR");
#endif
  auto kernelPair = get_reduction_params(_N);
  auto localSize = kernelPair.first;
  auto nWG = kernelPair.second;
  auto assignOp =
      make_AssignReduction<addAbsOp2_struct,INTERLOOP>(my_rs, my_vx, localSize, localSize * nWG);
//      make_addAbsAssignReduction<INTERLOOP>(my_rs, my_vx, localSize, localSize * nWG);
//      make_addAbsAssignReduction(my_rs, my_vx, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp, my_sc);
#else
  ex.reduce(assignOp);
#endif
#ifdef VERBOSE
  my_rs.printH("VR");
#endif
}

template <typename ExecutorType, typename T, typename ContainerT>
void _two_add(Executor<ExecutorType> ex, int _N,
              vector_view<T, ContainerT> _vx1, int _incx1,
              vector_view<T, ContainerT> _rs1, vector_view<T, ContainerT> _sc1,
//              vector_view<T, ContainerT> _rs1, // vector_view<T, ContainerT> _sc1,
              vector_view<T, ContainerT> _vx2, int _incx2,
              vector_view<T, ContainerT> _rs2, vector_view<T, ContainerT> _sc2){
//              vector_view<T, ContainerT> _rs2, vector_view<T, ContainerT> _sc) {
  // Common definitions
  auto kernelPair = get_reduction_params(_N);
  auto localSize = kernelPair.first;
  auto nWG = kernelPair.second;
  // _rs1 = add(_vx1)
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_rs1 = vector_view<T, ContainerT>(_rs1, _rs1.getDisp(), 1, 1);
  auto my_sc1 = vector_view<T, ContainerT>(_sc1, _sc1.getDisp(), 1, _N);
  auto assignOp1 =
      make_addAbsAssignReduction(my_rs1, my_vx1, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp1, my_sc1);
#else
  ex.reduce(assignOp1);
#endif
  // _rs2 = add(_vx2)
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_rs2 = vector_view<T, ContainerT>(_rs2, _rs2.getDisp(), 1, 1);
  auto my_sc2 = vector_view<T, ContainerT>(_sc2, _sc2.getDisp(), 1, _N);
  auto assignOp2 =
      make_addAbsAssignReduction(my_rs2, my_vx2, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp2, my_sc2);
#else
  ex.reduce(assignOp2);
#endif

}

template <typename ExecutorType, typename T, typename ContainerT>
void _two_addB(Executor<ExecutorType> ex, int _N,
              vector_view<T, ContainerT> _vx1, int _incx1,
//              vector_view<T, ContainerT> _rs1, vector_view<T, ContainerT> _sc1,
              vector_view<T, ContainerT> _rs1, // vector_view<T, ContainerT> _sc1,
              vector_view<T, ContainerT> _vx2, int _incx2,
//              vector_view<T, ContainerT> _rs2, vector_view<T, ContainerT> _sc2){
              vector_view<T, ContainerT> _rs2, vector_view<T, ContainerT> _sc) {
  // Common definitions
  auto kernelPair = get_reduction_params(_N);
  auto localSize = kernelPair.first;
  auto nWG = kernelPair.second;
  // _rs1 = add(_vx1)
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_rs1 = vector_view<T, ContainerT>(_rs1, _rs1.getDisp(), 1, 1);
  // _rs2 = add(_vx2)
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_rs2 = vector_view<T, ContainerT>(_rs2, _rs2.getDisp(), 1, 1);
  auto my_sc = vector_view<T, ContainerT>(_sc, _sc.getDisp(), 1, 2*_N);

  // concatenate both operations
  //  auto doubleAssignOp = make_op<Join>(assignOp1, assignOp2);
//  auto assignOp12 = make_AssignReduction_2Ops<addAbsOp2_struct>
  auto assignOp12 = make_AssignReduction_2Ops<addAbsOp2_struct,INTERLOOP>
                          (my_rs1, my_vx1, my_rs2, my_vx2,
                              localSize, localSize * nWG);
  // execute concatenated operations
  //  ex.reduce(doubleAssignOp);
  //  auto assignOp1 =
  //      make_addAbsAssignReduction(my_rs1, my_vx1, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce_2Ops(assignOp12, my_sc);
#else
  ex.reduce_2Ops(assignOp12);
#endif

}

template <typename ExecutorType, typename T, typename ContainerT>
void _four_add(Executor<ExecutorType> ex, int _N,
               vector_view<T, ContainerT> _vx1, int _incx1,
               vector_view<T, ContainerT> _rs1, vector_view<T, ContainerT> _sc1,
               vector_view<T, ContainerT> _vx2, int _incx2,
               vector_view<T, ContainerT> _rs2, vector_view<T, ContainerT> _sc2,
               vector_view<T, ContainerT> _vx3, int _incx3,
               vector_view<T, ContainerT> _rs3, vector_view<T, ContainerT> _sc3,
               vector_view<T, ContainerT> _vx4, int _incx4,
               vector_view<T, ContainerT> _rs4,
               vector_view<T, ContainerT> _sc4) {
  // Common definitions
  auto kernelPair = get_reduction_params(_N);
  auto localSize = kernelPair.first;
  auto nWG = kernelPair.second;

  // _rs1 = add(_vx1)
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_rs1 = vector_view<T, ContainerT>(_rs1, _rs1.getDisp(), 1, 1);
  auto my_sc1 = vector_view<T, ContainerT>(_sc1, _sc1.getDisp(), 1, 1);
  auto assignOp1 =
      make_addAbsAssignReduction(my_rs1, my_vx1, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp1, my_sc1);
#else
  ex.reduce(assignOp1);
#endif

  // _rs2 = add(_vx2)
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_rs2 = vector_view<T, ContainerT>(_rs2, _rs2.getDisp(), 1, 1);
  auto my_sc2 = vector_view<T, ContainerT>(_sc2, _sc2.getDisp(), 2, 2);
  auto assignOp2 =
      make_addAbsAssignReduction(my_rs2, my_vx2, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp2, my_sc2);
#else
  ex.reduce(assignOp2);
#endif

  // _rs3 = add(_vx3)
  auto my_vx3 = vector_view<T, ContainerT>(_vx3, _vx3.getDisp(), _incx3, _N);
  auto my_rs3 = vector_view<T, ContainerT>(_rs3, _rs3.getDisp(), 1, 1);
  auto my_sc3 = vector_view<T, ContainerT>(_sc3, _sc3.getDisp(), 3, 3);
  auto assignOp3 =
      make_addAbsAssignReduction(my_rs3, my_vx3, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp3, my_sc3);
#else
  ex.reduce(assignOp3);
#endif

  // _rs4 = add(_vx4)
  auto my_vx4 = vector_view<T, ContainerT>(_vx4, _vx4.getDisp(), _incx4, _N);
  auto my_rs4 = vector_view<T, ContainerT>(_rs4, _rs4.getDisp(), 1, 1);
  auto my_sc4 = vector_view<T, ContainerT>(_sc4, _sc4.getDisp(), 4, 4);
  auto assignOp4 =
      make_addAbsAssignReduction(my_rs4, my_vx4, localSize, localSize * nWG);
#ifdef REDUCE_SCRATCH
  ex.reduce(assignOp4, my_sc4);
#else
  ex.reduce(assignOp4);
#endif
  // concatenate operations
  //  auto doubleAssignOp12 = make_op<Join>(assignOp1, assignOp2);
  //  auto doubleAssignOp34 = make_op<Join>(assignOp3, assignOp4);
  //  auto quadAssignOp = make_op<Join>(doubleAssignOp12, doubleAssignOp34);

  // execute concatenated operations
  //  ex.execute(quadAssignOp);
}
template <typename ExecutorType, typename T, typename ContainerT>
void _four_addB(Executor<ExecutorType> ex, int _N,
               vector_view<T, ContainerT> _vx1, int _incx1,
//               vector_view<T, ContainerT> _rs1, vector_view<T, ContainerT> _sc1,
               vector_view<T, ContainerT> _rs1, //vector_view<T, ContainerT> _sc1,
               vector_view<T, ContainerT> _vx2, int _incx2,
//               vector_view<T, ContainerT> _rs2, vector_view<T, ContainerT> _sc2,
               vector_view<T, ContainerT> _rs2, //vector_view<T, ContainerT> _sc2,
               vector_view<T, ContainerT> _vx3, int _incx3,
//               vector_view<T, ContainerT> _rs3, vector_view<T, ContainerT> _sc3,
               vector_view<T, ContainerT> _rs3, //vector_view<T, ContainerT> _sc3,
               vector_view<T, ContainerT> _vx4, int _incx4,
//               vector_view<T, ContainerT> _rs4, vector_view<T, ContainerT> _sc4) {
               vector_view<T, ContainerT> _rs4, vector_view<T, ContainerT> _sc) {
  // Common definitions
  auto kernelPair = get_reduction_params(_N);
  auto localSize = kernelPair.first;
  auto nWG = kernelPair.second;

  // _rs1 = add(_vx1)
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_rs1 = vector_view<T, ContainerT>(_rs1, _rs1.getDisp(), 1, 1);
//  auto my_sc1 = vector_view<T, ContainerT>(_sc1, _sc1.getDisp(), 1, 1);
//  auto assignOp1 =
//      make_addAbsAssignReduction(my_rs1, my_vx1, localSize, localSize * nWG);
//#ifdef REDUCE_SCRATCH/
//  ex.reduce(assignOp1, my_sc1);
//#else
//  ex.reduce(assignOp1);
//#endif

  // _rs2 = add(_vx2)
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_rs2 = vector_view<T, ContainerT>(_rs2, _rs2.getDisp(), 1, 1);
//  auto my_sc2 = vector_view<T, ContainerT>(_sc2, _sc2.getDisp(), 2, 2);
//  auto assignOp2 =
//      make_addAbsAssignReduction(my_rs2, my_vx2, localSize, localSize * nWG);
//#ifdef REDUCE_SCRATCH
//  ex.reduce(assignOp2, my_sc2);
//#else
//  ex.reduce(assignOp2);
//#endif

  // _rs3 = add(_vx3)
  auto my_vx3 = vector_view<T, ContainerT>(_vx3, _vx3.getDisp(), _incx3, _N);
  auto my_rs3 = vector_view<T, ContainerT>(_rs3, _rs3.getDisp(), 1, 1);
//  auto my_sc3 = vector_view<T, ContainerT>(_sc3, _sc3.getDisp(), 3, 3);
//  auto assignOp3 =
//      make_addAbsAssignReduction(my_rs3, my_vx3, localSize, localSize * nWG);
//#ifdef REDUCE_SCRATCH
//  ex.reduce(assignOp3, my_sc3);
//#else
//  ex.reduce(assignOp3);
//#endif

  // _rs4 = add(_vx4)
  auto my_vx4 = vector_view<T, ContainerT>(_vx4, _vx4.getDisp(), _incx4, _N);
  auto my_rs4 = vector_view<T, ContainerT>(_rs4, _rs4.getDisp(), 1, 1);
  auto my_sc = vector_view<T, ContainerT>(_sc, _sc.getDisp(), 1, 4*_N);
//  auto my_sc4 = vector_view<T, ContainerT>(_sc4, _sc4.getDisp(), 4, 4);
//  auto assignOp4 =
//      make_addAbsAssignReduction(my_rs4, my_vx4, localSize, localSize * nWG);
//#ifdef REDUCE_SCRATCH
//  ex.reduce(assignOp4, my_sc4);
//#else
//  ex.reduce(assignOp4);
//#endif
  // concatenate operations
  auto assignOp1234 = make_AssignReduction_4Ops<addAbsOp2_struct,INTERLOOP>
                          (my_rs1, my_vx1, my_rs2, my_vx2,
                           my_rs3, my_vx3, my_rs4, my_vx4,
                              localSize, localSize * nWG);

  // execute concatenated operations
  #ifdef REDUCE_SCRATCH
    ex.reduce_4Ops(assignOp1234, my_sc);
  #else
    ex.reduce_4Ops(assignOp1234);
  #endif
}
// #########################
// #include <axpys.hpp>
// #########################
template <typename ExecutorType, typename T, typename ContainerT>
void _one_axpy(Executor<ExecutorType> ex, int _N, T _alpha,
               vector_view<T, ContainerT> _vx, int _incx,
               vector_view<T, ContainerT> _vy, int _incy) {
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, _N);
  auto my_vy = vector_view<T, ContainerT>(_vy, _vy.getDisp(), _incy, _N);
#ifdef VERBOSE
  std::cout << "alpha = " << _alpha << std::endl;
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif
  auto scalOp = make_op<ScalarOp, prdOp2_struct>(_alpha, my_vx);
  auto addOp = make_op<BinaryOp, addOp2_struct>(my_vy, scalOp);
  auto assignOp = make_op<Assign>(my_vy, addOp);
  //  ex.execute(assignOp);
  ex.execute(assignOp, LOCALSIZE);
#ifdef VERBOSE
  my_vy.printH("VY");
#endif
}
template <typename ExecutorType, typename T, typename ContainerT>
void _two_axpy(Executor<ExecutorType> ex, int _N, BASETYPE _alpha1,
               vector_view<T, ContainerT> _vx1, int _incx1,
               vector_view<T, ContainerT> _vy1, int _incy1, BASETYPE _alpha2,
               vector_view<T, ContainerT> _vx2, int _incx2,
               vector_view<T, ContainerT> _vy2, int _incy2) {
  // _vy1 = _alpha1 * _vx1 + _vy1
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_vy1 = vector_view<T, ContainerT>(_vy1, _vy1.getDisp(), _incy1, _N);
  auto scalOp1 = make_op<ScalarOp, prdOp2_struct>(_alpha1, my_vx1);
  auto addBinaryOp1 = make_op<BinaryOp, addOp2_struct>(my_vy1, scalOp1);

  // _vy2 = _alpha2 * _vx2 + _vy2
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_vy2 = vector_view<T, ContainerT>(_vy2, _vy2.getDisp(), _incy2, _N);
  auto scalOp2 = make_op<ScalarOp, prdOp2_struct>(_alpha2, my_vx2);
  auto addBinaryOp2 = make_op<BinaryOp, addOp2_struct>(my_vy2, scalOp2);

  // concatenate both operations
  auto assignOp1 = make_op<Assign>(my_vy1, addBinaryOp1);
  auto assignOp2 = make_op<Assign>(my_vy2, addBinaryOp2);
  auto doubleAssignOp = make_op<Join>(assignOp1, assignOp2);

  // execute concatenated operations
  //  ex.execute(doubleAssignOp);
  ex.execute(doubleAssignOp, LOCALSIZE);
}
template <typename ExecutorType, typename T, typename ContainerT>
void _four_axpy(Executor<ExecutorType> ex, int _N, BASETYPE _alpha1,
                vector_view<T, ContainerT> _vx1, int _incx1,
                vector_view<T, ContainerT> _vy1, int _incy1, BASETYPE _alpha2,
                vector_view<T, ContainerT> _vx2, int _incx2,
                vector_view<T, ContainerT> _vy2, int _incy2, BASETYPE _alpha3,
                vector_view<T, ContainerT> _vx3, int _incx3,
                vector_view<T, ContainerT> _vy3, int _incy3, BASETYPE _alpha4,
                vector_view<T, ContainerT> _vx4, int _incx4,
                vector_view<T, ContainerT> _vy4, int _incy4) {
  // _vy1 = _alpha1 * _vx1 + _vy1
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_vy1 = vector_view<T, ContainerT>(_vy1, _vy1.getDisp(), _incy1, _N);
  auto scalOp1 = make_op<ScalarOp, prdOp2_struct>(_alpha1, my_vx1);
  auto addBinaryOp1 = make_op<BinaryOp, addOp2_struct>(my_vy1, scalOp1);

  // _vy2 = _alpha2 * _vx2 + _vy2
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_vy2 = vector_view<T, ContainerT>(_vy2, _vy2.getDisp(), _incy2, _N);
  auto scalOp2 = make_op<ScalarOp, prdOp2_struct>(_alpha2, my_vx2);
  auto addBinaryOp2 = make_op<BinaryOp, addOp2_struct>(my_vy2, scalOp2);

  // _vy3 = _alpha3 * _vx3 + _vy3
  auto my_vx3 = vector_view<T, ContainerT>(_vx3, _vx3.getDisp(), _incx3, _N);
  auto my_vy3 = vector_view<T, ContainerT>(_vy3, _vy3.getDisp(), _incy3, _N);
  auto scalOp3 = make_op<ScalarOp, prdOp2_struct>(_alpha3, my_vx3);
  auto addBinaryOp3 = make_op<BinaryOp, addOp2_struct>(my_vy3, scalOp3);

  // _vy4 = _alpha4 * _vx4 + _vy4
  auto my_vx4 = vector_view<T, ContainerT>(_vx4, _vx4.getDisp(), _incx4, _N);
  auto my_vy4 = vector_view<T, ContainerT>(_vy4, _vy4.getDisp(), _incy4, _N);
  auto scalOp4 = make_op<ScalarOp, prdOp2_struct>(_alpha4, my_vx4);
  auto addBinaryOp4 = make_op<BinaryOp, addOp2_struct>(my_vy4, scalOp4);

  // concatenate operations
  auto assignOp1 = make_op<Assign>(my_vy1, addBinaryOp1);
  auto assignOp2 = make_op<Assign>(my_vy2, addBinaryOp2);
  auto assignOp3 = make_op<Assign>(my_vy3, addBinaryOp3);
  auto assignOp4 = make_op<Assign>(my_vy4, addBinaryOp4);
  auto doubleAssignOp12 = make_op<Join>(assignOp1, assignOp2);
  auto doubleAssignOp34 = make_op<Join>(assignOp3, assignOp4);
  auto quadAssignOp = make_op<Join>(doubleAssignOp12, doubleAssignOp34);

  // execute concatenated operations
  //  ex.execute(quadAssignOp);
  ex.execute(quadAssignOp, LOCALSIZE);
}
// #########################
// #include <copys.hpp>
// #########################
template <typename ExecutorType, typename T, typename ContainerT>
void _one_copy(Executor<ExecutorType> ex, int _N,
               vector_view<T, ContainerT> _vx, int _incx,
               vector_view<T, ContainerT> _vy, int _incy) {
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, _N);
  auto my_vy = vector_view<T, ContainerT>(_vy, _vy.getDisp(), _incy, _N);
#ifdef VERBOSE
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif
  auto assignOp = make_op<Assign>(my_vy, my_vx);
  //  ex.execute(assignOp);
  ex.execute(assignOp, LOCALSIZE);
#ifdef VERBOSE
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif
}
template <typename ExecutorType, typename T, typename ContainerT>
void _two_copy(Executor<ExecutorType> ex, int _N,
               vector_view<T, ContainerT> _vx1, int _incx1,
               vector_view<T, ContainerT> _vy1, int _incy1,
               vector_view<T, ContainerT> _vx2, int _incx2,
               vector_view<T, ContainerT> _vy2, int _incy2) {
  // _vy1 = _vx1
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_vy1 = vector_view<T, ContainerT>(_vy1, _vy1.getDisp(), _incy1, _N);
  auto assignOp1 = make_op<Assign>(my_vy1, my_vx1);

  // _vy2 = _vx2
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_vy2 = vector_view<T, ContainerT>(_vy2, _vy2.getDisp(), _incy2, _N);
  auto assignOp2 = make_op<Assign>(my_vy2, my_vx2);

  // concatenate both operations
  auto doubleAssignOp = make_op<Join>(assignOp1, assignOp2);

  // execute concatenated operations
  //  ex.execute(doubleAssignOp);
  ex.execute(doubleAssignOp, LOCALSIZE);
}
template <typename ExecutorType, typename T, typename ContainerT>
void _four_copy(Executor<ExecutorType> ex, int _N,
                vector_view<T, ContainerT> _vx1, int _incx1,
                vector_view<T, ContainerT> _vy1, int _incy1,
                vector_view<T, ContainerT> _vx2, int _incx2,
                vector_view<T, ContainerT> _vy2, int _incy2,
                vector_view<T, ContainerT> _vx3, int _incx3,
                vector_view<T, ContainerT> _vy3, int _incy3,
                vector_view<T, ContainerT> _vx4, int _incx4,
                vector_view<T, ContainerT> _vy4, int _incy4) {
  // _vy1 = _vx1
  auto my_vx1 = vector_view<T, ContainerT>(_vx1, _vx1.getDisp(), _incx1, _N);
  auto my_vy1 = vector_view<T, ContainerT>(_vy1, _vy1.getDisp(), _incy1, _N);
  auto assignOp1 = make_op<Assign>(my_vy1, my_vx1);

  // _vy2 = _vx2
  auto my_vx2 = vector_view<T, ContainerT>(_vx2, _vx2.getDisp(), _incx2, _N);
  auto my_vy2 = vector_view<T, ContainerT>(_vy2, _vy2.getDisp(), _incy2, _N);
  auto assignOp2 = make_op<Assign>(my_vy2, my_vx2);

  // _vy3 = _vx3
  auto my_vx3 = vector_view<T, ContainerT>(_vx3, _vx3.getDisp(), _incx3, _N);
  auto my_vy3 = vector_view<T, ContainerT>(_vy3, _vy3.getDisp(), _incy3, _N);
  auto assignOp3 = make_op<Assign>(my_vy3, my_vx3);

  // _vy4 = _vx4
  auto my_vx4 = vector_view<T, ContainerT>(_vx4, _vx4.getDisp(), _incx4, _N);
  auto my_vy4 = vector_view<T, ContainerT>(_vy4, _vy4.getDisp(), _incy4, _N);
  auto assignOp4 = make_op<Assign>(my_vy4, my_vx4);

  // concatenate operations
  auto doubleAssignOp12 = make_op<Join>(assignOp1, assignOp2);
  auto doubleAssignOp34 = make_op<Join>(assignOp3, assignOp4);
  auto quadAssignOp = make_op<Join>(doubleAssignOp12, doubleAssignOp34);

  // execute concatenated operations
  //  ex.execute(quadAssignOp);
  ex.execute(quadAssignOp, LOCALSIZE);
}
// #########################
int main(int argc, char *argv[]) {
  size_t numE, strd, sizeV, returnVal = 0;
  if (argc == 1) {
    numE = DEF_NUM_ELEM;
    strd = DEF_STRIDE;
  } else if (argc == 2) {
    numE = atoi(argv[1]);
    strd = DEF_STRIDE;
  } else if (argc == 3) {
    numE = atoi(argv[1]);
    strd = atoi(argv[2]);
  } else {
    std::cout << "ERROR!! --> Incorrect number of input parameters"
              << std::endl;
    returnVal = 1;
  }
  if (returnVal == 0) {
    sizeV = numE * strd;
#ifdef SHOW_TIMES
    // VARIABLES FOR TIMING
    std::chrono::time_point<std::chrono::steady_clock> t_start, t_stop;
    std::chrono::duration<BASETYPE> t0_copy, t0_axpy, t0_add;
    std::chrono::duration<BASETYPE> t1_copy, t1_axpy, t1_add;
    std::chrono::duration<BASETYPE> t2_copy, t2_axpy, t2_add;
    std::chrono::duration<BASETYPE> t3_copy, t3_axpy, t3_add;
    std::vector<std::chrono::duration<BASETYPE>> v0_copy(NUMBER_REPEATS), v0_axpy(NUMBER_REPEATS), v0_add(NUMBER_REPEATS);
    std::vector<std::chrono::duration<BASETYPE>> v1_copy(NUMBER_REPEATS), v1_axpy(NUMBER_REPEATS), v1_add(NUMBER_REPEATS);
    std::vector<std::chrono::duration<BASETYPE>> v2_copy(NUMBER_REPEATS), v2_axpy(NUMBER_REPEATS), v2_add(NUMBER_REPEATS);
    std::vector<std::chrono::duration<BASETYPE>> v3_copy(NUMBER_REPEATS), v3_axpy(NUMBER_REPEATS), v3_add(NUMBER_REPEATS);
#endif
    // CREATING DATA
    std::vector<BASETYPE> vX1(sizeV);
    std::vector<BASETYPE> vY1(sizeV);
    std::vector<BASETYPE> vZ1(sizeV);
    std::vector<BASETYPE> vS1(4);
    std::vector<BASETYPE> vX2(sizeV);
    std::vector<BASETYPE> vY2(sizeV);
    std::vector<BASETYPE> vZ2(sizeV);
    std::vector<BASETYPE> vS2(4);
    std::vector<BASETYPE> vX3(sizeV);
    std::vector<BASETYPE> vY3(sizeV);
    std::vector<BASETYPE> vZ3(sizeV);
    std::vector<BASETYPE> vS3(4);
    std::vector<BASETYPE> vX4(sizeV);
    std::vector<BASETYPE> vY4(sizeV);
    std::vector<BASETYPE> vZ4(sizeV);
    std::vector<BASETYPE> vS4(4);
    // CREATING INTERMEDIATE DATA
#ifdef ONE_SCRATCH
    std::vector<BASETYPE> vR (sizeV*4);
#else
    std::vector<BASETYPE> vR1(sizeV);
    std::vector<BASETYPE> vR2(sizeV);
    std::vector<BASETYPE> vR3(sizeV);
    std::vector<BASETYPE> vR4(sizeV);
#endif

    // INITIALIZING DATA
    size_t vSeed, gap;
    BASETYPE minV, maxV;
    vSeed = 1;
    minV = -10.0;
    maxV = 10.0;
    gap = (size_t)(maxV - minV + 1);
    srand(vSeed);
    std::for_each(std::begin(vX1), std::end(vX1),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    std::for_each(std::begin(vX2), std::end(vX2),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    std::for_each(std::begin(vX3), std::end(vX3),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    std::for_each(std::begin(vX4), std::end(vX4),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    vSeed = 1;
    minV = -30.0;
    maxV = 10.0;
    gap = (size_t)(maxV - minV + 1);
    srand(vSeed);
    std::for_each(std::begin(vZ1), std::end(vZ1),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    std::for_each(std::begin(vZ2), std::end(vZ2),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    std::for_each(std::begin(vZ3), std::end(vZ3),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    std::for_each(std::begin(vZ4), std::end(vZ4),
                  [&](BASETYPE &elem) { elem = minV + (BASETYPE)(rand() % gap); });
    // COMPUTING THE RESULTS
    int i;
    BASETYPE sum1 = 0.0f, alpha1 = 1.1f;
    BASETYPE sum2 = 0.0f, alpha2 = 2.2f;
    BASETYPE sum3 = 0.0f, alpha3 = 3.3f;
    BASETYPE sum4 = 0.0f, alpha4 = 4.4f;
    BASETYPE ONE = 1.0f;
    i = 0;
    std::for_each(std::begin(vY1), std::end(vY1), [&](BASETYPE &elem) {
      elem = vZ1[i] + alpha1 * vX1[i];
      if ((i % strd) == 0) sum1 += std::abs(elem);
      i++;
    });
    //    vS1[0] = sum1;
    i = 0;
    std::for_each(std::begin(vY2), std::end(vY2), [&](BASETYPE &elem) {
      elem = vZ2[i] + alpha2 * vX2[i];
      if ((i % strd) == 0) sum2 += std::abs(elem);
      i++;
    });
    //    vS2[0] = sum2;
    i = 0;
    std::for_each(std::begin(vY3), std::end(vY3), [&](BASETYPE &elem) {
      elem = vZ3[i] + alpha3 * vX3[i];
      if ((i % strd) == 0) sum3 += std::abs(elem);
      i++;
    });
    //    vS3[0] = sum3;
    i = 0;
    std::for_each(std::begin(vY4), std::end(vY4), [&](BASETYPE &elem) {
      elem = vZ4[i] + alpha4 * vX4[i];
      if ((i % strd) == 0) sum4 += std::abs(elem);
      i++;
    });
    //    vS4[0] = sum4;
    // CREATING THE SYCL QUEUE AND EXECUTOR
#ifdef GPU
    cl::sycl::gpu_selector   s;
#else
    cl::sycl::intel_selector s;
    // cl::sycl::cpu_selector s; NOOOOO
#endif
    cl::sycl::queue q(s,[=](cl::sycl::exception_list eL) {
//    cl::sycl::queue q([=](cl::sycl::exception_list eL) {
      try {
        for (auto &e : eL) {
          std::rethrow_exception(e);
        }
      } catch (cl::sycl::exception &e) {
        std::cout << " E " << e.what() << std::endl;
      } catch (...) {
        std::cout << " An exception " << std::endl;
      }
    });
    Executor<SYCL> ex(q);
    {
      // CREATION OF THE BUFFERS
      buffer<BASETYPE, 1> bX1(vX1.data(), range<1>{vX1.size()});
      buffer<BASETYPE, 1> bY1(vY1.data(), range<1>{vY1.size()});
      buffer<BASETYPE, 1> bZ1(vZ1.data(), range<1>{vZ1.size()});
      buffer<BASETYPE, 1> bS1(vS1.data(), range<1>{vS1.size()});
      buffer<BASETYPE, 1> bX2(vX2.data(), range<1>{vX2.size()});
      buffer<BASETYPE, 1> bY2(vY2.data(), range<1>{vY2.size()});
      buffer<BASETYPE, 1> bZ2(vZ2.data(), range<1>{vZ2.size()});
      buffer<BASETYPE, 1> bS2(vS2.data(), range<1>{vS2.size()});
      buffer<BASETYPE, 1> bX3(vX3.data(), range<1>{vX3.size()});
      buffer<BASETYPE, 1> bY3(vY3.data(), range<1>{vY3.size()});
      buffer<BASETYPE, 1> bZ3(vZ3.data(), range<1>{vZ3.size()});
      buffer<BASETYPE, 1> bS3(vS3.data(), range<1>{vS3.size()});
      buffer<BASETYPE, 1> bX4(vX4.data(), range<1>{vX4.size()});
      buffer<BASETYPE, 1> bY4(vY4.data(), range<1>{vY4.size()});
      buffer<BASETYPE, 1> bZ4(vZ4.data(), range<1>{vZ4.size()});
      buffer<BASETYPE, 1> bS4(vS4.data(), range<1>{vS4.size()});
      // CREATION OF THE SCRATCH
#ifdef ONE_SCRATCH
      buffer<BASETYPE, 1> bR1(vR.data(), range<1>{vY1.size()});
      buffer<BASETYPE, 1> bR2(vR.data()+vY1.size(), range<1>{vY2.size()});
      buffer<BASETYPE, 1> bR3(vR.data()+vY1.size()+vY2.size(), range<1>{vY3.size()});
      buffer<BASETYPE, 1> bR4(vR.data()+vY1.size()+vY2.size()+vY3.size(), range<1>{vY4.size()});
      buffer<BASETYPE, 1> bR12(vR.data(), range<1>{vY1.size()+vY2.size()});
      buffer<BASETYPE, 1> bR34(vR.data()+vY1.size()+vY2.size(), range<1>{vY3.size()+vY4.size()});
      buffer<BASETYPE, 1> bR1234(vR.data(), range<1>{vY1.size()+vY2.size()+vY3.size()+vY4.size()});
#else
      buffer<BASETYPE, 1> bR1(vR1.data(), range<1>{vR1.size()});
      buffer<BASETYPE, 1> bR2(vR2.data(), range<1>{vR2.size()});
      buffer<BASETYPE, 1> bR3(vR3.data(), range<1>{vR3.size()});
      buffer<BASETYPE, 1> bR4(vR4.data(), range<1>{vR4.size()});
#endif

//      buffer<BASETYPE, 1> bR (vR .data(), range<1>{vR .size()});
      // BUILDING A SYCL VIEW OF THE BUFFERS
      BufferVectorView<BASETYPE> bvX1(bX1);
      BufferVectorView<BASETYPE> bvY1(bY1);
      BufferVectorView<BASETYPE> bvZ1(bZ1);
      BufferVectorView<BASETYPE> bvS1(bS1);
      BufferVectorView<BASETYPE> bvX2(bX2);
      BufferVectorView<BASETYPE> bvY2(bY2);
      BufferVectorView<BASETYPE> bvZ2(bZ2);
      BufferVectorView<BASETYPE> bvS2(bS2);
      BufferVectorView<BASETYPE> bvX3(bX3);
      BufferVectorView<BASETYPE> bvY3(bY3);
      BufferVectorView<BASETYPE> bvZ3(bZ3);
      BufferVectorView<BASETYPE> bvS3(bS3);
      BufferVectorView<BASETYPE> bvX4(bX4);
      BufferVectorView<BASETYPE> bvY4(bY4);
      BufferVectorView<BASETYPE> bvZ4(bZ4);
      BufferVectorView<BASETYPE> bvS4(bS4);
#ifdef ONE_SCRATCH
      BufferVectorView<BASETYPE> bvR1(bR1);
      BufferVectorView<BASETYPE> bvR2(bR2);
      BufferVectorView<BASETYPE> bvR3(bR3);
      BufferVectorView<BASETYPE> bvR4(bR4);
      BufferVectorView<BASETYPE> bvR12(bR12);
      BufferVectorView<BASETYPE> bvR34(bR34);
      BufferVectorView<BASETYPE> bvR1234(bR1234);
#else
      BufferVectorView<BASETYPE> bvR1(bR1);
      BufferVectorView<BASETYPE> bvR2(bR2);
      BufferVectorView<BASETYPE> bvR3(bR3);
      BufferVectorView<BASETYPE> bvR4(bR4);
#endif

      // Force update here to avoid including memory copies on the
      // benchmark
      q.submit([&](codeplay::handler &h) {
        auto accX1 = bX1.get_access<access::mode::write>(h);
        auto accX2 = bX2.get_access<access::mode::write>(h);
        auto accX3 = bX3.get_access<access::mode::write>(h);
        auto accX4 = bX4.get_access<access::mode::write>(h);

        auto accY1 = bY1.get_access<access::mode::write>(h);
        auto accY2 = bY2.get_access<access::mode::write>(h);
        auto accY3 = bY3.get_access<access::mode::write>(h);
        auto accY4 = bY4.get_access<access::mode::write>(h);

        auto accZ1 = bZ1.get_access<access::mode::write>(h);
        auto accZ2 = bZ2.get_access<access::mode::write>(h);
        auto accZ3 = bZ3.get_access<access::mode::write>(h);
        auto accZ4 = bZ4.get_access<access::mode::write>(h);

        auto accR1 = bR1.get_access<access::mode::write>(h);
        auto accR2 = bR2.get_access<access::mode::write>(h);
        auto accR3 = bR3.get_access<access::mode::write>(h);
        auto accR4 = bR4.get_access<access::mode::write>(h);

        auto accR12 = bR12.get_access<access::mode::write>(h);
        auto accR34 = bR34.get_access<access::mode::write>(h);

        auto accR1234 = bR1234.get_access<access::mode::write>(h);

        h.update_to_device(accX1);
        h.update_to_device(accX2);
        h.update_to_device(accX3);
        h.update_to_device(accX4);

        h.update_to_device(accY1);
        h.update_to_device(accY2);
        h.update_to_device(accY3);
        h.update_to_device(accY4);

        h.update_to_device(accZ1);
        h.update_to_device(accZ2);
        h.update_to_device(accZ3);
        h.update_to_device(accZ4);

        h.update_to_device(accR1);
        h.update_to_device(accR2);
        h.update_to_device(accR3);
        h.update_to_device(accR4);

        h.update_to_device(accR12);
        h.update_to_device(accR34);

        h.update_to_device(accR1234);
      });
      q.wait_and_throw();

      for (int i = 0; i < NUMBER_REPEATS; i++) {
// EXECUTION OF THE ROUTINES (FOR CLBLAS)
#ifdef TEST_COPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _one_copy<SYCL>(ex, numE, bvZ1, strd, bvY1, strd);
        _one_copy<SYCL>(ex, numE, bvZ2, strd, bvY2, strd);
        _one_copy<SYCL>(ex, numE, bvZ3, strd, bvY3, strd);
        _one_copy<SYCL>(ex, numE, bvZ4, strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t0_copy = t_stop - t_start;
        } else if (i > 0) {
          t0_copy += t_stop - t_start;
        } else {
          t0_copy = t_start - t_start;
        }
        v0_copy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_AXPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _one_axpy<SYCL>(ex, numE, alpha1, bvX1, strd, bvY1, strd);
        _one_axpy<SYCL>(ex, numE, alpha2, bvX2, strd, bvY2, strd);
        _one_axpy<SYCL>(ex, numE, alpha3, bvX3, strd, bvY3, strd);
        _one_axpy<SYCL>(ex, numE, alpha4, bvX4, strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t0_axpy = t_stop - t_start;
        } else if (i > 0) {
          t0_axpy += t_stop - t_start;
        } else {
          t0_axpy = t_start - t_start;
        }
        v0_axpy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_ADD
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _one_add<SYCL>(ex, numE, bvY1, strd, bvS1, bvR1);
        _one_add<SYCL>(ex, numE, bvY2, strd, bvS2, bvR2);
        _one_add<SYCL>(ex, numE, bvY3, strd, bvS3, bvR3);
        _one_add<SYCL>(ex, numE, bvY4, strd, bvS4, bvR4);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t0_add = t_stop - t_start;
        } else if (i > 0) {
          t0_add += t_stop - t_start;
        } else {
          t0_add = t_start - t_start;
        }
        v0_add[i] = t_stop - t_start;
  #endif
#endif
// EXECUTION OF THE ROUTINES (SINGLE OPERATIONS)
#ifdef TEST_COPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _one_copy<SYCL>(ex, numE, bvZ1, strd, bvY1, strd);
        _one_copy<SYCL>(ex, numE, bvZ2, strd, bvY2, strd);
        _one_copy<SYCL>(ex, numE, bvZ3, strd, bvY3, strd);
        _one_copy<SYCL>(ex, numE, bvZ4, strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t1_copy = t_stop - t_start;
        } else if (i > 0) {
          t1_copy += t_stop - t_start;
        } else {
          t1_copy = t_start - t_start;
        }
        v1_copy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_AXPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _one_axpy<SYCL>(ex, numE, alpha1, bvX1, strd, bvY1, strd);
        _one_axpy<SYCL>(ex, numE, alpha2, bvX2, strd, bvY2, strd);
        _one_axpy<SYCL>(ex, numE, alpha3, bvX3, strd, bvY3, strd);
        _one_axpy<SYCL>(ex, numE, alpha4, bvX4, strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t1_axpy = t_stop - t_start;
        } else if (i > 0) {
          t1_axpy += t_stop - t_start;
        } else {
          t1_axpy = t_start - t_start;
        }
        v1_axpy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_ADD
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _one_add<SYCL>(ex, numE, bvY1, strd, bvS1 + 1, bvR1);
        _one_add<SYCL>(ex, numE, bvY2, strd, bvS2 + 1, bvR2);
        _one_add<SYCL>(ex, numE, bvY3, strd, bvS3 + 1, bvR3);
        _one_add<SYCL>(ex, numE, bvY4, strd, bvS4 + 1, bvR4);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t1_add = t_stop - t_start;
        } else if (i > 0) {
          t1_add += t_stop - t_start;
        } else {
          t1_add = t_start - t_start;
        }
        v1_add[i] = t_stop - t_start;
  #endif
#endif
// EXECUTION OF THE ROUTINES (DOUBLE OPERATIONS)
#ifdef TEST_COPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _two_copy<SYCL>(ex, numE, bvZ1, strd, bvY1, strd, bvZ2, strd, bvY2,
                        strd);
        _two_copy<SYCL>(ex, numE, bvZ3, strd, bvY3, strd, bvZ4, strd, bvY4,
                        strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t2_copy = t_stop - t_start;
        } else if (i > 0) {
          t2_copy += t_stop - t_start;
        } else {
          t2_copy = t_start - t_start;
        }
        v2_copy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_AXPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _two_axpy<SYCL>(ex, numE, alpha1, bvX1, strd, bvY1, strd, alpha2, bvX2,
                        strd, bvY2, strd);
        _two_axpy<SYCL>(ex, numE, alpha3, bvX3, strd, bvY3, strd, alpha4, bvX4,
                        strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t2_axpy = t_stop - t_start;
        } else if (i > 0) {
          t2_axpy += t_stop - t_start;
        } else {
          t2_axpy = t_start - t_start;
        }
        v2_axpy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_ADD
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
  #ifdef FUSION_TWO_ADDS
        _two_addB<SYCL>(ex, numE, bvY1, strd, bvS1 + 2, bvY2, strd,
                        bvS2 + 2, bvR12);
        _two_addB<SYCL>(ex, numE, bvY3, strd, bvS3 + 2, bvY4, strd,
                        bvS4 + 2, bvR34);
  #else
        _two_add<SYCL>(ex, numE, bvY1, strd, bvS1 + 2, bvR1, bvY2, strd,
                        bvS2 + 2, bvR2);
        _two_add<SYCL>(ex, numE, bvY3, strd, bvS3 + 2, bvR3, bvY4, strd,
                        bvS4 + 2, bvR4);
  #endif
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t2_add = t_stop - t_start;
        } else if (i > 0) {
          t2_add += t_stop - t_start;
        } else {
          t2_add = t_start - t_start;
        }
        v2_add[i] = t_stop - t_start;
  #endif
#endif
// EXECUTION OF THE ROUTINES (QUADUBLE OPERATIONS)
#ifdef TEST_COPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _four_copy<SYCL>(ex, numE, bvZ1, strd, bvY1, strd, bvZ2, strd, bvY2,
                         strd, bvZ3, strd, bvY3, strd, bvZ4, strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t3_copy = t_stop - t_start;
        } else if (i > 0) {
          t3_copy += t_stop - t_start;
        } else {
          t3_copy = t_start - t_start;
        }
        v3_copy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_AXPY
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
        _four_axpy<SYCL>(ex, numE, alpha1, bvX1, strd, bvY1, strd, alpha2, bvX2,
                         strd, bvY2, strd, alpha3, bvX3, strd, bvY3, strd,
                         alpha4, bvX4, strd, bvY4, strd);
        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t3_axpy = t_stop - t_start;
        } else if (i > 0) {
          t3_axpy += t_stop - t_start;
        } else {
          t3_axpy = t_start - t_start;
        }
        v3_axpy[i] = t_stop - t_start;
  #endif
#endif
#ifdef TEST_ADD
  #ifdef SHOW_TIMES
        t_start = std::chrono::steady_clock::now();
  #endif
  #ifdef FUSION_FOUR_ADDS
        _four_addB<SYCL>(ex, numE, bvY1, strd, bvS1 + 3, bvY2, strd,
                        bvS2 + 3, bvY3, strd, bvS3 + 3, bvY4, strd,
                        bvS4 + 3, bvR1234);
  #else
    #ifdef FUSION_TWO_ADDS
        _two_addB<SYCL>(ex, numE, bvY1, strd, bvS1 + 3, bvY2, strd,
                        bvS2 + 3, bvR12);
        _two_addB<SYCL>(ex, numE, bvY3, strd, bvS3 + 3, bvY4, strd,
                        bvS4 + 3, bvR34);
    #else
        _four_add<SYCL>(ex, numE, bvY1, strd, bvS1 + 3, bvR1, bvY2, strd,
                        bvS2 + 3, bvR2, bvY3, strd, bvS3 + 3, bvR3, bvY4, strd,
                        bvS4 + 3, bvR4);
    #endif
  #endif

        q.wait_and_throw();
  #ifdef SHOW_TIMES
        t_stop = std::chrono::steady_clock::now();
        if (NUMBER_REPEATS == 1) {
          t3_add = t_stop - t_start;
        } else if (i > 0) {
          t3_add  += t_stop - t_start;
        } else {
          t3_add = t_start - t_start;
        }
        v3_add[i] = t_stop - t_start;
  #endif
#endif
      }
    }
#ifdef SHOW_TIMES
    int div = (NUMBER_REPEATS == 1)? 1: (NUMBER_REPEATS-1);
    // COMPUTATIONAL TIMES
    std::cout << "t_copy , " << t0_copy.count()/div << ", " << t1_copy.count()/div
              << ", " << t2_copy.count()/div << ", " << t3_copy.count()/div
              << std::endl;
    std::cout << "t_axpy , " << t0_axpy.count()/div << ", " << t1_axpy.count()/div
              << ", " << t2_axpy.count()/div << ", " << t3_axpy.count()/div
              << std::endl;
    std::cout << "t_add  , " << t0_add.count()/div << ", " << t1_add.count()/div << ", "
              << t2_add.count()/div << ", " << t3_add.count()/div << std::endl;

    std::sort (v0_copy.begin()+1, v0_copy.end());
    std::sort (v1_copy.begin()+1, v1_copy.end());
    std::sort (v2_copy.begin()+1, v2_copy.end());
    std::sort (v3_copy.begin()+1, v3_copy.end());
    std::cout << "m_copy , " << v0_copy[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v1_copy[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v2_copy[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v3_copy[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v0_axpy.begin()+1, v0_axpy.end());
    std::sort (v1_axpy.begin()+1, v1_axpy.end());
    std::sort (v2_axpy.begin()+1, v2_axpy.end());
    std::sort (v3_axpy.begin()+1, v3_axpy.end());
    std::cout << "m_axpy , " << v0_axpy[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v1_axpy[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v2_axpy[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v3_axpy[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v0_add.begin()+1, v0_add.end());
    std::sort (v1_add.begin()+1, v1_add.end());
    std::sort (v2_add.begin()+1, v2_add.end());
    std::sort (v3_add.begin()+1, v3_add.end());
    std::cout << "m_add  , " << v0_add[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v1_add[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v2_add[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v3_add[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
/*  ERROR
    for (const auto time:v0_copy) std::cout << time.count() << " ";
    std::cout << std::endl;
    std::sort (v0_copy.begin()+1, v0_copy.end());
    for (const auto time:v0_copy) std::cout << time.count() << " ";
    std::cout << " -> £" << v0_copy[(NUMBER_REPEATS+1)/2].count() << std::endl;
*/
#endif
    // ANALYSIS OF THE RESULTS
    BASETYPE res;
    for (i = 0; i < 4; i++) {
      res = vS1[i];
#ifdef SHOW_VALUES
      std::cout << "VALUES!! --> res = " << res << " , i = " << i
                << " , sum = " << sum1 << " , err = " << res - sum1
                << std::endl;
#endif  //  SHOW_VALUES
      if (std::abs((res - sum1) / res) > ERROR_ALLOWED) {
        std::cout << " (A) ERROR!! --> res = " << res << " , i = " << i
                  << " , sum = " << sum1 << " , err = " << res - sum1
                  << std::endl;
        returnVal += 2 * i;
      }
    }
    for (i = 0; i < 4; i++) {
      res = vS2[i];
#ifdef SHOW_VALUES
      std::cout << "VALUES!! --> res = " << res << " , i = " << i
                << " , sum = " << sum2 << " , err = " << res - sum2
                << std::endl;
#endif  //  SHOW_VALUES
      if (std::abs((res - sum2) / res) > ERROR_ALLOWED) {
        std::cout << " (B) ERROR!! --> res = " << res << " , i = " << i
                  << " , sum = " << sum2 << " , err = " << res - sum2
                  << std::endl;
        returnVal += 20 * i;
      }
    }
    for (i = 0; i < 4; i++) {
      res = vS3[i];
#ifdef SHOW_VALUES
      std::cout << "VALUES!! --> res = " << res << " , i = " << i
                << " , sum = " << sum3 << " , err = " << res - sum3
                << std::endl;
#endif  //  SHOW_VALUES
      if (std::abs((res - sum3) / res) > ERROR_ALLOWED) {
        std::cout << " (C) ERROR!! --> res = " << res << " , i = " << i
                  << " , sum = " << sum3 << " , err = " << res - sum3
                  << std::endl;
        returnVal += 200 * i;
      }
    }
    for (i = 0; i < 4; i++) {
      res = vS4[i];
#ifdef SHOW_VALUES
      std::cout << "VALUES!! --> res = " << res << " , i = " << i
                << " , sum = " << sum4 << " , err = " << res - sum4
                << std::endl;
#endif  //  SHOW_VALUES
      if (std::abs((res - sum4) / res) > ERROR_ALLOWED) {
        std::cout << " (D) ERROR!! --> res = " << res << " , i = " << i
                  << " , sum = " << sum4 << " , err = " << res - sum4
                  << std::endl;
        returnVal += 2000 * i;
      }
    }
  }
  return returnVal;
}