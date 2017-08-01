#include <algorithm>
#include <cstdlib>
#include <interface/blas1_interface_sycl.hpp>
#include <interface/blas2_interface_sycl.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace cl::sycl;
using namespace blas;

#define DEF_SIZE_VECT 1200
#define ERROR_ALLOWED 1.0E-8
#define RANDOM_DATA 1
#define EXECUTED_ON_GPU 1
#define SHOW_VALUES   1

#define BASETYPE double

#define SHOW_TIMES 1  // If it exists, the code prints the execution time
                      // The ... should be changed by the corresponding routine
#define NUMBER_REPEATS 6  // Number of times the computations are made
// If it is greater than 1, the compile time is not considered

#ifdef EXECUTED_ON_GPU
#define DEFAULT_ACCESS false
#else
#define DEFAULT_ACCESS true
#endif

// INITIAL MATRIZ VECTOR PRODUCT

template <typename ExecutorType, typename T, typename ContainerT>
void _gemv0(Executor<ExecutorType> ex, std::string _Trans, size_t _M, size_t _N,
            T _alpha, matrix_view<T, ContainerT> _mA, size_t _lda,
            vector_view<T, ContainerT> _vx, size_t _incx, T _beta,
            vector_view<T, ContainerT> _vy, size_t _incy) {
  if ((_Trans[0] != 'n') && (_Trans[0] != 'N') && (_Trans[0] != 't') &&
      (_Trans[0] != 'T') && (_Trans[0] != 'c') && (_Trans[0] != 'C'))
    std::cout << "Erroneous parameter" << std::endl;
  bool accessOpr = ((_Trans[0] == 'n') || (_Trans[0] == 'N'));
  size_t M = (accessOpr ? _M : _N);
  size_t N = (accessOpr ? _N : _M);
  auto my_mA =
      matrix_view<T, ContainerT>(_mA, _M, _N, accessOpr, _lda, _mA.getDisp());
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, N);
  auto my_vy = vector_view<T, ContainerT>(_vy, _vy.getDisp(), _incy, M);
#ifdef VERBOSE
  std::cout << "alpha = " << _alpha << " , beta = " << _beta << std::endl;
  my_mA.printH("MA");
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif  // VERBOSE
  if (my_mA.getAccess()) {
    auto scalOp = make_op<ScalarOp, prdOp2_struct>(_beta, my_vy);
    auto assignOp = make_op<Assign>(my_vy, scalOp);
    ex.execute(assignOp);
    auto disp = my_mA.getDisp();
    for (size_t i = 0; i < M; i++) {
      auto my_row = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, N);
      auto my_rs = vector_view<T, ContainerT>(my_vy, i + my_vy.getDisp(), 1, 1);
      auto scl = my_rs.eval(0);
      auto prdOp1 = make_op<BinaryOp, prdOp2_struct>(my_row, my_vx);
      auto localSize = 256;
      auto nWG = 128;
#ifdef SYCL_CODE
      auto assignOp1 =
          make_addAssignReduction(my_rs, prdOp1, localSize, localSize * nWG);
      ex.reduce(assignOp1);
#else   // SYCL_CODE
      ContainerT valT1(nWG);
      auto val1 = vector_view<T, ContainerT>(valT1, 0, 1, nWG);
      auto assignOp11 =
          make_addAssignReduction(val1, prdOp1, localSize, localSize * nWG);
      ex.execute(assignOp11);
      auto assignOp1 = make_addAssignReduction(my_rs, val1, localSize, nWG);
      ex.execute(assignOp1);
#endif  // SYCL_CODE
      auto prdOp2 = make_op<ScalarOp, prdOp2_struct>(_alpha, my_rs);
      auto addOp2 = make_op<ScalarOp, addOp2_struct>(scl, prdOp2);
      auto assignOp2 = make_op<Assign>(my_rs, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  } else {
    auto scalOp1 = make_op<ScalarOp, prdOp2_struct>(_beta, my_vy);
    auto assignOp1 = make_op<Assign>(my_vy, scalOp1);
    ex.execute(assignOp1);
    auto disp = my_mA.getDisp();
    for (size_t j = 0; j < N; j++) {
      auto my_col = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, M);
      auto scalOp2 =
          make_op<ScalarOp, prdOp2_struct>(_alpha * my_vx.eval(j), my_col);
      auto addOp2 = make_op<BinaryOp, addOp2_struct>(my_vy, scalOp2);
      auto assignOp2 = make_op<Assign>(my_vy, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  }
#ifdef VERBOSE
  my_vy.printH("VY");
#endif  // VERBOSE
}

// INITIAL RANK 1 MODIFICATION

template <typename ExecutorType, typename T, typename ContainerT>
void _ger0(Executor<ExecutorType> ex, size_t _M, size_t _N, T _alpha,
           vector_view<T, ContainerT> _vx, size_t _incx,
           vector_view<T, ContainerT> _vy, size_t _incy,
           matrix_view<T, ContainerT> _mA, size_t _lda) {
  bool accessOpr = true;
  size_t M = _M;
  size_t N = _N;
  auto my_mA =
      matrix_view<T, ContainerT>(_mA, _M, _N, accessOpr, _lda, _mA.getDisp());
  auto my_vx = vector_view<T, ContainerT>(_vx, _vx.getDisp(), _incx, M);
  auto my_vy = vector_view<T, ContainerT>(_vy, _vy.getDisp(), _incy, N);
#ifdef VERBOSE
  std::cout << "alpha = " << _alpha << std::endl;
  my_mA.printH("MA");
  my_vx.printH("VX");
  my_vy.printH("VY");
#endif  // VERBOSE
  if (my_mA.getAccess()) {
    auto disp = my_mA.getDisp();
    for (size_t i = 0; i < M; i++) {
      auto my_row = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, N);
      auto scalOp2 =
          make_op<ScalarOp, prdOp2_struct>(_alpha * my_vx.eval(i), my_vy);
      auto addOp2 = make_op<BinaryOp, addOp2_struct>(my_row, scalOp2);
      auto assignOp2 = make_assign(my_row, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  } else {
    auto disp = my_mA.getDisp();
    for (size_t j = 0; j < N; j++) {
      auto my_col = vector_view<T, ContainerT>(my_mA.getData(), disp, 1, M);
      auto scalOp2 =
          make_op<ScalarOp, prdOp2_struct>(_alpha * my_vy.eval(j), my_vx);
      auto addOp2 = make_op<BinaryOp, addOp2_struct>(my_col, scalOp2);
      auto assignOp2 = make_assign(my_col, addOp2);
      ex.execute(assignOp2);
      disp += _lda;
    }
  }
#ifdef VERBOSE
  my_vy.printH("VY");
#endif  // VERBOSE
}

// TESTING ROUTINE

size_t TestingBLAS2(bool accessDev, size_t dim, size_t divSz, size_t shftR,
                    size_t shftC) {
  // CREATING DATA
  size_t dimR = dim / divSz;
  size_t dimC = dim * divSz;
  std::vector<BASETYPE> vM0(dimR * dimC);
  std::vector<BASETYPE> vM1(dimR * dimC);
  std::vector<BASETYPE> vX0(dimC);
  std::vector<BASETYPE> vY0(dimR);
  std::vector<BASETYPE> vX1(dimC);
  std::vector<BASETYPE> vY1(dimR);
  std::vector<BASETYPE> vX2(dimC);
  std::vector<BASETYPE> vY2(dimR);
  std::vector<BASETYPE> vR(3);
  std::vector<BASETYPE> vS(3);
  std::vector<BASETYPE> vT(3);
#ifdef SHOW_TIMES
  std::chrono::time_point<std::chrono::steady_clock> t_start, t_stop;
  std::chrono::duration<BASETYPE> t1_gmvR, t1_gmvC, t1_ger;
  std::chrono::duration<BASETYPE> t2_gmvR, t2_gmvC, t2_ger;
  std::chrono::duration<BASETYPE> t3_gmvR, t3_gmvC, t3_ger;
  std::vector<std::chrono::duration<BASETYPE>> v1_gmvR(NUMBER_REPEATS), v1_gmvC(NUMBER_REPEATS), v1_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v2_gmvR(NUMBER_REPEATS), v2_gmvC(NUMBER_REPEATS), v2_ger(NUMBER_REPEATS);
  std::vector<std::chrono::duration<BASETYPE>> v3_gmvR(NUMBER_REPEATS), v3_gmvC(NUMBER_REPEATS), v3_ger(NUMBER_REPEATS);
#endif

  // INITIALIZING DATA
  size_t vSeed, gap;
  BASETYPE minV, maxV;
#ifdef RANDOM_DATA
  vSeed = 1;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vM1), std::end(vM1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA

  });

#ifdef RANDOM_DATA
  vSeed = 1;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vX0), std::end(vX0), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 2;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vX1), std::end(vX1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 3;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vY0), std::end(vY0), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

#ifdef RANDOM_DATA
  vSeed = 4;
  minV = -10.0;
  maxV = 10.0;
  gap = (size_t)(maxV - minV + 1);
  srand(vSeed);
#else   // RANDOM_DATA
  minV = 1.00;
  maxV = 1.00;
#endif  // RANDOM_DATA
  std::for_each(std::begin(vY1), std::end(vY1), [&](BASETYPE &elem) {
#ifdef RANDOM_DATA
    elem = minV + (BASETYPE)(rand() % gap);
#else   // RANDOM_DATA
    elem = minV; minV += maxV;
#endif  // RANDOM_DATA
  });

  for (int i=0; i<3; i++) {
    vR[i] = 0.0;
    vS[i] = 0.0;
    vT[i] = 0.0;
  }

  // CREATING HOST STRUCTURES
  size_t dimL = ((accessDev) ? dimC : dimR);
  matrix_view<BASETYPE, std::vector<BASETYPE>> v_M0(vM0, accessDev, dimR, dimC, true,
                                                dimL, 0);
  matrix_view<BASETYPE, std::vector<BASETYPE>> v_M1(vM1, accessDev, dimR, dimC, true,
                                                dimL, 0);
  vector_view<BASETYPE, std::vector<BASETYPE>> v_X0(vX0, 0, 1, dimC);
  vector_view<BASETYPE, std::vector<BASETYPE>> v_Y0(vY0, 0, 1, dimR);
  vector_view<BASETYPE, std::vector<BASETYPE>> v_R(vR, 0, 1, 1);

  // COMPUTING THE RESULTS
  size_t returnVal = 0;
  BASETYPE res;

  BASETYPE addY = 0.0, auxY;
  for (size_t i = shftR; i < dimR; i++) {
//    vY2[i - shftR] = 1.5 * vY[i - shftR];
    auxY = 1.5 * vY1[i - shftR];
//    printf ("(AB) %f = 1.5 * %f\n", auxY, vY2[i - shftC]);
    for (size_t j = shftC; j < dimC; j++) {
      if (accessDev) {
//        vY2[i - shftR] += 2.0 * vM[dimC * i + j] * vY[j - shftC];
//        printf ("(A) %f += 2.0 * %f * %f\n", auxY, vM[dimC * i + j], vY[j - shftC]);
        auxY += 2.0 * vM1[dimC * i + j] * vY0[j - shftC];
      } else {
//        vY2[i - shftR] += 2.0 * vM[dimR * j + i] * vY[j - shftC];
//        printf ("(B) %f += 2.0 * %f * %f\n", auxY, vM[dimR * j + i], vY[j - shftC]);
        auxY += 2.0 * vM1[dimR * j + i] * vY0[j - shftC];
      }
    }
//    addY += vY2[i - shftR];
    addY += auxY;
//    printf("VY2(%lu) = %f\n", i, auxY);
  }
  for (size_t i = dimR - shftR; i < dimR; i++) {
#ifdef VERBOSE
    std::cout << "+" << vY[i] << std::endl;
#endif  // VERBOSE
    addY += vY1[i];
  }

  BASETYPE addX = 0.0, auxX;
  for (size_t j = shftC; j < dimC; j++) {
//    vX2[j - shftC] = 0.5 * vX2[j - shftC];
    auxX = 0.5 * vX1[j - shftC];
    for (size_t i = shftR; i < dimR; i++) {
      if (accessDev) {
//        vX2[j - shftC] += 2.5 * vM[dimC * i + j] * vX[i - shftR];
        auxX += 2.5 * vM1[dimC * i + j] * vX0[i - shftR];
      } else {
//        vX2[j - shftC] += 2.5 * vM[dimR * j + i] * vX[i - shftR];
        auxX += 2.5 * vM1[dimR * j + i] * vX0[i - shftR];
      }
    }
//    addX += vX2[j - shftC];
//    printf("VX2(%lu) = %f\n", j, auxX);
    addX += auxX;
  }
  for (size_t j = dimC - shftC; j < dimC; j++) {
    addX += vX1[j];
  }

  BASETYPE addRng1 = 0.0;
  for (size_t i = 0; i < dimR; i++) {
    for (size_t j = 0; j < dimC; j++) {
      addRng1 += (accessDev) ? vM1[dimC * i + j] : vM1[dimR * j + i];
      if ((i >= shftR) && (j >= shftC)) {
        addRng1 += 3.0 * vY0[i - shftR] * vX0[j - shftC];
      }
    }
  }

  // CREATING THE SYCL QUEUE AND EXECUTOR
  cl::sycl::queue q([=](cl::sycl::exception_list eL) {
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
    buffer<BASETYPE, 1> bM0(vM0.data(), range<1>{vM0.size()});
    buffer<BASETYPE, 1> bM1(vM1.data(), range<1>{vM1.size()});
    buffer<BASETYPE, 1> bX0(vX0.data(), range<1>{vX0.size()});
    buffer<BASETYPE, 1> bY0(vY0.data(), range<1>{vY0.size()});
    buffer<BASETYPE, 1> bX1(vX1.data(), range<1>{vX1.size()});
    buffer<BASETYPE, 1> bY1(vY1.data(), range<1>{vY1.size()});
    buffer<BASETYPE, 1> bX2(vX2.data(), range<1>{vX2.size()});
    buffer<BASETYPE, 1> bY2(vY2.data(), range<1>{vY2.size()});
    buffer<BASETYPE, 1> bR(vR.data(), range<1>{vR.size()});
    buffer<BASETYPE, 1> bS(vS.data(), range<1>{vS.size()});
    buffer<BASETYPE, 1> bT(vT.data(), range<1>{vT.size()});

    // BUILDING A SYCL VIEW OF THE BUFFERS
    BufferMatrixView<BASETYPE> bmM0(bM0, accessDev, dimR, dimC);
    BufferMatrixView<BASETYPE> bmM1(bM1, accessDev, dimR, dimC);
    BufferVectorView<BASETYPE> bvV0(bM0);
    BufferVectorView<BASETYPE> bvX0(bX0);
    BufferVectorView<BASETYPE> bvY0(bY0);
    BufferVectorView<BASETYPE> bvX1(bX1);
    BufferVectorView<BASETYPE> bvY1(bY1);
    BufferVectorView<BASETYPE> bvX2(bX2);
    BufferVectorView<BASETYPE> bvY2(bY2);
    BufferVectorView<BASETYPE> bvR1(bR,0);
    BufferVectorView<BASETYPE> bvR2(bR,1);
    BufferVectorView<BASETYPE> bvS1(bS,0);
    BufferVectorView<BASETYPE> bvS2(bS,1);
    BufferVectorView<BASETYPE> bvS3(bS,2);
    BufferVectorView<BASETYPE> bvT(bT);

    // EXECUTION OF THE ROUTINES
    for (int i = 0; i < NUMBER_REPEATS; i++) {
      /*****************************************/
      auto assign_M0 = make_op<Assign>(bmM0, bmM1);
      ex.execute(assign_M0); q.wait_and_throw();

      /*****************************************/
      auto assign_X2_1 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<1, SYCL>(ex, "Tr", dimC - shftC, dimR - shftR, 2.5, bmM0(shftR, shftC),
                  dimL, bvX0, 1, 0.5, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t1_gmvR += t_stop - t_start;
      } else {
        t1_gmvR = t_start - t_start;
      }
      v1_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX1 = make_addAssignReduction(bvR1, bvX2, 256, 256);
      ex.reduce(reducOpX1); q.wait_and_throw();
      /*****************************************/
      auto assign_X2_2 = make_op<Assign>(bvX2, bvX1);
      ex.execute(assign_X2_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<2, SYCL>(ex, "Tr", dimC - shftC, dimR - shftR, 2.5, bmM0(shftR, shftC),
                  dimL, bvX0, 1, 0.5, bvX2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_gmvR = t_stop - t_start;
      } else if (i > 0) {
        t2_gmvR += t_stop - t_start;
      } else {
        t2_gmvR = t_start - t_start;
      }
      v2_gmvR[i] = t_stop - t_start;
  #endif
      auto reducOpX2 = make_addAssignReduction(bvR2, bvX2, 256, 256);
      ex.reduce(reducOpX2); q.wait_and_throw();
      /*****************************************/
//      auto assign1_Seg = make_op<Assign>(bvX2, bvY2);
//      ex.execute(assign1_Seg); q.wait_and_throw();
      auto assign_Y2_1 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_1); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<1, SYCL>(ex, "No", dimR - shftR, dimC - shftC, 2.0, bmM0(shftR, shftC),
                  dimL, bvY0, 1, 1.5, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t1_gmvC += t_stop - t_start;
      } else {
        t1_gmvC = t_start - t_start;
      }
      v1_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY1 = make_addAssignReduction(bvS1, bvY2, 256, 256);
      ex.reduce(reducOpY1); q.wait_and_throw();
//      auto assign1_Rec = make_op<Assign>(bvY2, bvX2);
//      ex.execute(assign1_Rec); q.wait_and_throw();
      /*****************************************/
      auto assign_Y2_2 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_2); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<2, SYCL>(ex, "No", dimR - shftR, dimC - shftC, 2.0, bmM0(shftR, shftC),
                  dimL, bvY0, 1, 1.5, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t2_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t2_gmvC += t_stop - t_start;
      } else {
        t2_gmvC = t_start - t_start;
      }
      v2_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY2 = make_addAssignReduction(bvS2, bvY2, 256, 256);
      ex.reduce(reducOpY2); q.wait_and_throw();
//      auto assign2_Rec = make_op<Assign>(bvY2, bvX2);
//      ex.execute(assign2_Rec); q.wait_and_throw();
      /*****************************************/
      auto assign_Y2_3 = make_op<Assign>(bvY2, bvY1);
      ex.execute(assign_Y2_3); q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _gemv<3, SYCL>(ex, "No", dimR - shftR, dimC - shftC, 2.0, bmM0(shftR, shftC),
                  dimL, bvY0, 1, 1.5, bvY2, 1);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t3_gmvC = t_stop - t_start;
      } else if (i > 0) {
        t3_gmvC += t_stop - t_start;
      } else {
        t3_gmvC = t_start - t_start;
      }
      v3_gmvC[i] = t_stop - t_start;
  #endif
      auto reducOpY3 = make_addAssignReduction(bvS3, bvY2, 256, 256);
      ex.reduce(reducOpY3); q.wait_and_throw();
      /*****************************************/
  #ifdef SHOW_TIMES
      t_start = std::chrono::steady_clock::now();
  #endif
      _ger<SYCL>(ex, dimR - shftR, dimC - shftC, 3.0, bvY0, 1, bvX0, 1,
                 bmM0(shftR, shftC), dimL);
      q.wait_and_throw();
  #ifdef SHOW_TIMES
      t_stop = std::chrono::steady_clock::now();
      if (NUMBER_REPEATS == 1) {
        t1_ger = t_stop - t_start;
      } else if (i > 0) {
        t1_ger += t_stop - t_start;
      } else {
        t1_ger = t_start - t_start;
      }
      v1_ger[i] = t_stop - t_start;
  #endif
      auto reducOpV = make_addAssignReduction(bvT, bvV0, 256, 256);
      ex.reduce(reducOpV);

      q.wait_and_throw();
    }
  }

#ifdef SHOW_TIMES
    int div = (NUMBER_REPEATS == 1)? 1: (NUMBER_REPEATS-1);
//    int div = 1;
    // COMPUTATIONAL TIMES
    std::cout << "t_gemvR , " << t1_gmvR.count()/div
              << ", " << t2_gmvR.count()/div
              << std::endl;
    std::cout << "t_gemvC , " << t1_gmvC.count()/div
              << ", " << t2_gmvC.count()/div
              << ", " << t3_gmvC.count()/div
              << std::endl;
    std::cout << "t_ger   , " << t1_ger.count()/div << std::endl;
    std::sort (v1_gmvR.begin()+1, v1_gmvR.end());
    std::sort (v2_gmvR.begin()+1, v2_gmvR.end());
    std::cout << "m_gmvC , " << v1_gmvR[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v2_gmvR[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v1_gmvC.begin()+1, v1_gmvC.end());
    std::sort (v2_gmvC.begin()+1, v2_gmvC.end());
    std::sort (v3_gmvC.begin()+1, v3_gmvC.end());
    std::cout << "m_gmvC , " << v1_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v2_gmvC[(NUMBER_REPEATS+1)/2].count()
              << ", "        << v3_gmvC[(NUMBER_REPEATS+1)/2].count()
              << std::endl;
    std::sort (v1_ger.begin()+1, v1_ger.end());
    std::cout << "m_gmvC , " << v1_ger[(NUMBER_REPEATS+1)/2].count()
              << std::endl;

#endif

  // ANALYSIS OF THE RESULTS
  for (int i=0; i<2; i++) {
    res = vR[i];
#ifdef SHOW_VALUES
    std::cout << "VALUES!! --> res = " << res << " , addX = " << addX
              << " , err = " << addX - res << std::endl;
#endif  // VERBOSE
    if (std::abs((res - addX) / res) > ERROR_ALLOWED) {
      std::cout << "ERROR!! --> res = " << res << " , addX = " << addX
                << " , err = " << addX - res << std::endl;
      returnVal += 1;
    }
  }

  for (int i=0; i<3; i++) {
    res = vS[i];
  #ifdef SHOW_VALUES
    std::cout << "VALUES!! --> res = " << res << " , addY = " << addY
              << " , err = " << addY - res << std::endl;
  #endif  // VERBOSE
    if (std::abs((res - addY) / res) > ERROR_ALLOWED) {
      std::cout << "ERROR!! --> res = " << res << " , addY = " << addY
                << " , err = " << addY - res << std::endl;
      returnVal += 2;
    }
  }

  res = vT[0];
#ifdef SHOW_VALUES
  std::cout << "VALUES!! --> res = " << res << " , addRng1 = " << addRng1
            << " , err = " << addRng1 - res << std::endl;
#endif  // VERBOSE
  if (std::abs((res - addRng1) / res) > ERROR_ALLOWED) {
    std::cout << "ERROR!! --> res = " << res << " , addRng1 = " << addRng1
              << " , err = " << addRng1 - res << std::endl;
    returnVal += 2;
  }

  return returnVal;
}

int main(int argc, char *argv[]) {
  //  using namespace SyclBlas;
  bool accessDev = DEFAULT_ACCESS;
  size_t sizeV = 0, divSz = 1, shftR = 0, shftC = 0;
  size_t returnVal = 0;

  if (argc == 1) {
    sizeV = DEF_SIZE_VECT;
  } else if (argc == 2) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
  } else if (argc == 3) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
    divSz = atoi(argv[2]);
    ;
  } else if (argc == 4) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      //      accessDev = false;
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
    shftR = atoi(argv[2]);
    shftC = atoi(argv[3]);
  } else if (argc == 5) {
    if (atoi(argv[1]) < 0) {
      sizeV = -atoi(argv[1]);
      //      accessDev = false;
      accessDev = !DEFAULT_ACCESS;
    } else
      sizeV = atoi(argv[1]);
    divSz = atoi(argv[2]);
    ;
    shftR = atoi(argv[3]);
    shftC = atoi(argv[4]);
  } else {
    std::cout << "ERROR!! --> Incorrect number of input parameters"
              << std::endl;
    returnVal = 1;
  }

  if (returnVal == 0)
    returnVal = 2 * TestingBLAS2(accessDev, sizeV, divSz, shftR, shftC);

  return returnVal;
}
