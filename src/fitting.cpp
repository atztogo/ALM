/*
 fitting.cpp

 Copyright (c) 2014-2018 Terumasa Tadano

 This file is distributed under the terms of the MIT license.
 Please see the file 'LICENCE.txt' in the root directory
 or http://opensource.org/licenses/mit-license.php for information.
*/

#include "fitting.h"
#include "constants.h"
#include "constraint.h"
#include "error.h"
#include "fcs.h"
#include "input_parser.h"
#include "mathfunctions.h"
#include "memory.h"
#include "symmetry.h"
#include "timer.h"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <boost/lexical_cast.hpp>

#ifdef WITH_SPARSE_SOLVER
#include <Eigen/Dense>
#include <Eigen/SparseCore>
#include <Eigen/SparseQR>
#include <Eigen/SparseCholesky>
//#include <unsupported/Eigen/SparseExtra>
//#include <bench/BenchTimer.h>
#endif

using namespace ALM_NS;

Fitting::Fitting()
{
    set_default_variables();
}

Fitting::~Fitting()
{
    deallocate_variables();
}

void Fitting::set_default_variables()
{
    params = nullptr;
    u_in = nullptr;
    f_in = nullptr;
    ndata = 0;
    nstart = 1;
    nend = 0;
    skip_s = 0;
    skip_e = 0;
    ndata_used = 0;
    use_sparseQR = 0;

    // moved from lasso.cpp
    disp_norm = 1.0;
    l1_alpha = 1.0;
    lasso_tol = 1.0e-7;
    maxiter = 100000;
    lasso_cv = 0;
    lasso_cvset = 10;
    output_frequency = 1000;
    l1_alpha_min = 1.0e-3;
    l1_alpha_max = 1.0;
    num_l1_alpha = 100;
    standardize = 1;
    ndata_test = 0;
    nstart_test = 0;
    nend_test = 0;
    save_solution_path = 0;
    debias_ols = 0;
}

void Fitting::deallocate_variables()
{
    if (params) {
        deallocate(params);
    }
    if (u_in) {
        deallocate(u_in);
    }
    if (f_in) {
        deallocate(f_in);
    }
}

int Fitting::optimize_main(const Symmetry *symmetry,
                           const Constraint *constraint,
                           const Fcs *fcs,
                           const int maxorder,
                           const unsigned int nat,
                           const int verbosity,
                           const std::string file_disp,
                           const std::string file_force,
                           Timer *timer)
{
    timer->start_clock("fitting");

    const int natmin = symmetry->nat_prim;
    const int nconsts = constraint->number_of_constraints;
    const int ndata_used = nend - nstart + 1;
    const int ntran = symmetry->ntran;
    int info_fitting;

    int N = 0;
    for (auto i = 0; i < maxorder; ++i) {
        N += fcs->nequiv[i].size();
    }
    int M = 3 * natmin * static_cast<long>(ndata_used) * ntran;

    if (verbosity > 0) {
        std::cout << " FITTING" << std::endl;
        std::cout << " =======" << std::endl << std::endl;

        std::cout << "  Reference files" << std::endl;
        std::cout << "   Displacement: " << file_disp << std::endl;
        std::cout << "   Force       : " << file_force << std::endl;
        std::cout << std::endl;

        std::cout << "  NSTART = " << nstart << "; NEND = " << nend << std::endl;
        std::cout << "  " << ndata_used << " entries will be used for fitting."
            << std::endl << std::endl;

        std::cout << "  Total Number of Parameters : " << N
            << std::endl << std::endl;
    }

    std::vector<double> amat;
    std::vector<double> bvec;
    std::vector<double> param_tmp(N);

    if (constraint->constraint_algebraic) {

        // Apply constraints algebraically. (ICONST = 2, 3 is not supported.)
        // SPARSE = 1 is used only when the constraints are considered algebraically.

        int N_new = 0;
        for (auto i = 0; i < maxorder; ++i) {
            N_new += constraint->index_bimap[i].size();
        }
        if (verbosity > 0) {
            std::cout << "  Total Number of Free Parameters : "
                << N_new << std::endl << std::endl;
        }

        // Calculate matrix elements for fitting

        double fnorm;
        const unsigned long nrows = 3 * static_cast<long>(natmin)
            * static_cast<long>(ndata_used)
            * static_cast<long>(ntran);

        const unsigned long ncols = static_cast<long>(N_new);

        if (use_sparseQR) {

            // Use a solver for sparse matrix (Requires less memory for sparse inputs.)

#ifdef WITH_SPARSE_SOLVER
            SpMat sp_amat(nrows, ncols);
            Eigen::VectorXd sp_bvec(nrows);

            get_matrix_elements_in_sparse_form(maxorder,
                                               ndata_used,
                                               sp_amat,
                                               sp_bvec,
                                               fnorm,
                                               symmetry,
                                               fcs,
                                               constraint);

            std::cout << "Now, start fitting ..." << std::endl;

            info_fitting = run_eigen_sparseQR(sp_amat,
                                              sp_bvec,
                                              param_tmp,
                                              fnorm,
                                              maxorder,
                                              fcs,
                                              constraint,
                                              verbosity);
#else
            std::cout << " Please recompile the code with -DWITH_SPARSE_SOLVER" << std::endl;
            exit("optimize_main", "Sparse solver not supported.");
#endif

        } else {

            // Use a direct solver for a dense matrix

            amat.resize(nrows * ncols, 0.0);
            bvec.resize(nrows, 0.0);

            get_matrix_elements_algebraic_constraint(maxorder,
                                                     ndata_used,
                                                     &amat[0],
                                                     &bvec[0],
                                                     fnorm,
                                                     symmetry,
                                                     fcs,
                                                     constraint);

            // Perform fitting with SVD

            info_fitting
                = fit_algebraic_constraints(N_new, M,
                                            &amat[0], &bvec[0],
                                            param_tmp,
                                            fnorm, maxorder,
                                            fcs,
                                            constraint,
                                            verbosity);
        }

    } else {

        // Apply constraints numerically (ICONST=2 is supported)

        if (use_sparseQR) {
            std::cout << "  WARNING: SPARSE = 1 works only with ICONST = 10 or ICONST = 11." << std::endl;
            std::cout << "  Use a solver for dense matrix." << std::endl;
        }

        // Calculate matrix elements for fitting

        const unsigned long nrows = 3 * static_cast<long>(natmin)
            * static_cast<long>(ndata_used)
            * static_cast<long>(ntran);

        const unsigned long ncols = static_cast<long>(N);

        amat.resize(nrows * ncols, 0.0);
        bvec.resize(nrows, 0.0);

        get_matrix_elements(maxorder,
                            ndata_used,
                            &amat[0],
                            &bvec[0],
                            symmetry,
                            fcs);

        // Perform fitting with SVD or QRD

        assert(!amat.empty());
        assert(!bvec.empty());

        if (constraint->exist_constraint) {
            info_fitting
                = fit_with_constraints(N, M, nconsts,
                                       &amat[0], &bvec[0],
                                       &param_tmp[0],
                                       constraint->const_mat,
                                       constraint->const_rhs,
                                       verbosity);
        } else {
            info_fitting
                = fit_without_constraints(N, M,
                                          &amat[0], &bvec[0],
                                          &param_tmp[0],
                                          verbosity);
        }
    }

    // Copy force constants to public variable "params"
    if (params) {
        deallocate(params);
    }
    allocate(params, N);
    for (auto i = 0; i < N; ++i) params[i] = param_tmp[i];

    if (verbosity > 0) {
        std::cout << std::endl;
        timer->print_elapsed();
        std::cout << " -------------------------------------------------------------------" << std::endl;
        std::cout << std::endl;
    }

    timer->stop_clock("fitting");

    return info_fitting;
}

void Fitting::set_displacement_and_force(const double * const *disp_in,
                                         const double * const *force_in,
                                         const int nat,
                                         const int ndata_used_in)
{
    ndata_used = ndata_used_in;

    if (u_in) {
        deallocate(u_in);
    }
    allocate(u_in, ndata_used, 3 * nat);

    if (f_in) {
        deallocate(f_in);
    }
    allocate(f_in, ndata_used, 3 * nat);

    for (int i = 0; i < ndata_used; i++) {
        for (int j = 0; j < 3 * nat; j++) {
            u_in[i][j] = disp_in[i][j];
            f_in[i][j] = force_in[i][j];
        }
    }
}

void Fitting::set_fcs_values(const int maxorder,
                             double *fc_in,
                             std::vector<int> *nequiv,
                             const Constraint *constraint)
{
    // fc_in: irreducible set of force constants
    // fc_length: dimension of params (can differ from that of fc_in)

    int i;

    int N = 0;
    int Nirred = 0;
    for (i = 0; i < maxorder; ++i) {
        N += nequiv[i].size();
        Nirred += constraint->index_bimap[i].size();
    }

    std::vector<double> param_in(Nirred, 0.0);
    std::vector<double> param_out(N, 0.0);

    for (i = 0; i < Nirred; ++i) {
        param_in[i] = fc_in[i];
    }
    recover_original_forceconstants(maxorder,
                                    param_in, param_out,
                                    nequiv, constraint);
    if (params) {
        deallocate(params);
    }
    allocate(params, N);
    for (i = 0; i < N; ++i) {
        params[i] = param_out[i];
    }
}

int Fitting::get_ndata_used() const
{
    return ndata_used;
}

int Fitting::fit_without_constraints(int N,
                                     int M,
                                     double *amat,
                                     double *bvec,
                                     double *param_out,
                                     const int verbosity) const
{
    int i, j;
    int nrhs = 1, nrank, INFO;
    auto rcond = -1.0;
    auto f_square = 0.0;
    double *WORK, *S, *fsum2;


    auto LMIN = std::min<int>(M, N);
    auto LMAX = std::max<int>(M, N);

    int LWORK = 3 * LMIN + std::max<int>(2 * LMIN, LMAX);
    LWORK = 2 * LWORK;

    if (verbosity > 0)
        std::cout << "  Entering fitting routine: SVD without constraints" << std::endl;


    allocate(WORK, LWORK);
    allocate(S, LMIN);
    allocate(fsum2, LMAX);

    unsigned long k = 0;

    for (i = 0; i < M; ++i) {
        fsum2[i] = bvec[i];
        f_square += std::pow(bvec[i], 2);
    }
    for (i = M; i < LMAX; ++i) fsum2[i] = 0.0;

    if (verbosity > 0) std::cout << "  SVD has started ... ";

    // Fitting with singular value decomposition
    dgelss_(&M, &N, &nrhs, amat, &M, fsum2, &LMAX,
            S, &rcond, &nrank, WORK, &LWORK, &INFO);

    if (verbosity > 0) {
        std::cout << "finished !" << std::endl << std::endl;
        std::cout << "  RANK of the matrix = " << nrank << std::endl;
    }

    if (nrank < N)
        warn("fit_without_constraints",
             "Matrix is rank-deficient. Force constants could not be determined uniquely :(");

    if (nrank == N && verbosity > 0) {
        auto f_residual = 0.0;
        for (i = N; i < M; ++i) {
            f_residual += std::pow(fsum2[i], 2);
        }
        std::cout << std::endl << "  Residual sum of squares for the solution: "
            << sqrt(f_residual) << std::endl;
        std::cout << "  Fitting error (%) : "
            << sqrt(f_residual / f_square) * 100.0 << std::endl;
    }

    for (i = 0; i < N; ++i) {
        param_out[i] = fsum2[i];
    }

    deallocate(WORK);
    deallocate(S);
    deallocate(fsum2);

    return INFO;
}

int Fitting::fit_with_constraints(int N,
                                  int M,
                                  int P,
                                  double *amat,
                                  double *bvec,
                                  double *param_out,
                                  double **cmat,
                                  double *dvec,
                                  const int verbosity) const
{
    int i, j;
    double *fsum2;
    double *mat_tmp;

    if (verbosity > 0)
        std::cout << "  Entering fitting routine: QRD with constraints" << std::endl;

    allocate(fsum2, M);
    allocate(mat_tmp, (M + P) * N);

    unsigned long k = 0;
    unsigned long l = 0;

    // Concatenate two matrices as 1D array
    for (j = 0; j < N; ++j) {
        for (i = 0; i < M; ++i) {
            mat_tmp[k++] = amat[l++];
        }
        for (i = 0; i < P; ++i) {
            mat_tmp[k++] = cmat[i][j];
        }
    }

    const auto nrank = rankQRD((M + P), N, mat_tmp, eps12);
    deallocate(mat_tmp);

    if (nrank != N) {
        std::cout << std::endl;
        std::cout << " **************************************************************************" << std::endl;
        std::cout << "  WARNING : rank deficient.                                                " << std::endl;
        std::cout << "  rank ( (A) ) ! = N            A: Fitting matrix     B: Constraint matrix " << std::endl;
        std::cout << "       ( (B) )                  N: The number of parameters                " << std::endl;
        std::cout << "  rank = " << nrank << " N = " << N << std::endl << std::endl;
        std::cout << "  This can cause a difficulty in solving the fitting problem properly      " << std::endl;
        std::cout << "  with DGGLSE, especially when the difference is large. Please check if    " << std::endl;
        std::cout << "  you obtain reliable force constants in the .fcs file.                    " << std::endl << std::
            endl;
        std::cout << "  You may need to reduce the cutoff radii and/or increase NDATA            " << std::endl;
        std::cout << "  by giving linearly-independent displacement patterns.                    " << std::endl;
        std::cout << " **************************************************************************" << std::endl;
        std::cout << std::endl;
    }

    auto f_square = 0.0;
    for (i = 0; i < M; ++i) {
        fsum2[i] = bvec[i];
        f_square += std::pow(bvec[i], 2);
    }
    if (verbosity > 0) std::cout << "  QR-Decomposition has started ...";

    double *cmat_mod;
    allocate(cmat_mod, P * N);

    k = 0;
    for (j = 0; j < N; ++j) {
        for (i = 0; i < P; ++i) {
            cmat_mod[k++] = cmat[i][j];
        }
    }

    // Fitting

    int LWORK = P + std::min<int>(M, N) + 10 * std::max<int>(M, N);
    int INFO;
    double *WORK, *x;
    allocate(WORK, LWORK);
    allocate(x, N);

    dgglse_(&M, &N, &P, amat, &M, cmat_mod, &P,
            fsum2, dvec, x, WORK, &LWORK, &INFO);

    if (verbosity > 0) std::cout << " finished. " << std::endl;

    double f_residual = 0.0;
    for (i = N - P; i < M; ++i) {
        f_residual += std::pow(fsum2[i], 2);
    }

    if (verbosity > 0) {
        std::cout << std::endl << "  Residual sum of squares for the solution: "
            << sqrt(f_residual) << std::endl;
        std::cout << "  Fitting error (%) : "
            << std::sqrt(f_residual / f_square) * 100.0 << std::endl;
    }

    // copy fcs to bvec
    for (i = 0; i < N; ++i) {
        param_out[i] = x[i];
    }

    deallocate(cmat_mod);
    deallocate(WORK);
    deallocate(x);
    deallocate(fsum2);

    return INFO;
}

int Fitting::fit_algebraic_constraints(int N,
                                       int M,
                                       double *amat,
                                       double *bvec,
                                       std::vector<double> &param_out,
                                       const double fnorm,
                                       const int maxorder,
                                       const Fcs *fcs,
                                       const Constraint *constraint,
                                       const int verbosity) const
{
    int i, j;
    unsigned long k;
    int nrhs = 1, nrank, INFO, LWORK;
    int LMIN, LMAX;
    double rcond = -1.0;
    double *WORK, *S, *fsum2;

    if (verbosity > 0)
        std::cout << "  Entering fitting routine: SVD with constraints considered algebraically." << std::endl;

    LMIN = std::min<int>(M, N);
    LMAX = std::max<int>(M, N);

    LWORK = 3 * LMIN + std::max<int>(2 * LMIN, LMAX);
    LWORK = 2 * LWORK;

    allocate(WORK, LWORK);
    allocate(S, LMIN);
    allocate(fsum2, LMAX);

    for (i = 0; i < M; ++i) {
        fsum2[i] = bvec[i];
    }
    for (i = M; i < LMAX; ++i) fsum2[i] = 0.0;

    if (verbosity > 0) std::cout << "  SVD has started ... ";

    // Fitting with singular value decomposition
    dgelss_(&M, &N, &nrhs, amat, &M, fsum2, &LMAX,
            S, &rcond, &nrank, WORK, &LWORK, &INFO);

    deallocate(WORK);
    deallocate(S);

    if (verbosity > 0) {
        std::cout << "finished !" << std::endl << std::endl;
        std::cout << "  RANK of the matrix = " << nrank << std::endl;
    }

    if (nrank < N) {
        warn("fit_without_constraints",
             "Matrix is rank-deficient. Force constants could not be determined uniquely :(");
    }

    if (nrank == N && verbosity > 0) {
        double f_residual = 0.0;
        for (i = N; i < M; ++i) {
            f_residual += std::pow(fsum2[i], 2);
        }
        std::cout << std::endl;
        std::cout << "  Residual sum of squares for the solution: "
            << sqrt(f_residual) << std::endl;
        std::cout << "  Fitting error (%) : "
            << sqrt(f_residual / (fnorm * fnorm)) * 100.0 << std::endl;
    }

    std::vector<double> param_irred(N, 0.0);
    for (i = 0; i < LMIN; ++i) param_irred[i] = fsum2[i];
    deallocate(fsum2);

    // Recover reducible set of force constants

    recover_original_forceconstants(maxorder,
                                    param_irred,
                                    param_out,
                                    fcs->nequiv,
                                    constraint);

    return INFO;
}


void Fitting::get_matrix_elements(const int maxorder,
                                  const int ndata_fit,
                                  double *amat,
                                  double *bvec,
                                  const Symmetry *symmetry,
                                  const Fcs *fcs) const
{
    int i, j;
    long irow;

    std::vector<std::vector<double>> u_multi, f_multi;

    data_multiplier(u_in, u_multi, ndata_fit, symmetry);
    data_multiplier(f_in, f_multi, ndata_fit, symmetry);

    const int natmin = symmetry->nat_prim;
    const int natmin3 = 3 * natmin;
    const int nrows = natmin3 * ndata_fit * symmetry->ntran;
    auto ncols = 0;

    for (i = 0; i < maxorder; ++i) ncols += fcs->nequiv[i].size();

    const long ncycle = static_cast<long>(ndata_fit) * symmetry->ntran;

#ifdef _OPENMP
#pragma omp parallel private(irow, i, j)
#endif
    {
        int *ind;
        int mm, order, iat, k;
        int im, iparam;
        long idata;
        double amat_tmp;
        double **amat_orig_tmp;

        allocate(ind, maxorder + 1);
        allocate(amat_orig_tmp, natmin3, ncols);

#ifdef _OPENMP
#pragma omp for schedule(guided)
#endif
        for (irow = 0; irow < ncycle; ++irow) {

            // generate r.h.s vector B
            for (i = 0; i < natmin; ++i) {
                iat = symmetry->map_p2s[i][0];
                for (j = 0; j < 3; ++j) {
                    im = 3 * i + j + natmin3 * irow;
                    bvec[im] = f_multi[irow][3 * iat + j];
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    amat_orig_tmp[i][j] = 0.0;
                }
            }

            // generate l.h.s. matrix A

            idata = natmin3 * irow;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                mm = 0;

                for (const auto &iter : fcs->nequiv[order]) {
                    for (i = 0; i < iter; ++i) {
                        ind[0] = fcs->fc_table[order][mm].elems[0];
                        k = inprim_index(ind[0], symmetry);
                        amat_tmp = 1.0;
                        for (j = 1; j < order + 2; ++j) {
                            ind[j] = fcs->fc_table[order][mm].elems[j];
                            amat_tmp *= u_multi[irow][fcs->fc_table[order][mm].elems[j]];
                        }
                        amat_orig_tmp[k][iparam] -= gamma(order + 2, ind) * fcs->fc_table[order][mm].sign * amat_tmp;
                        ++mm;
                    }
                    ++iparam;
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    // Transpose here for later use of lapack without transpose
                    amat[natmin3 * ncycle * j + i + idata] = amat_orig_tmp[i][j];
                }
            }
        }

        deallocate(ind);
        deallocate(amat_orig_tmp);
    }

    u_multi.clear();
    f_multi.clear();
}


void Fitting::get_matrix_elements_algebraic_constraint(const int maxorder,
                                                       const int ndata_fit,
                                                       double *amat,
                                                       double *bvec,
                                                       double &fnorm,
                                                       const Symmetry *symmetry,
                                                       const Fcs *fcs,
                                                       const Constraint *constraint) const
{
    int i, j;
    long irow;

    std::vector<std::vector<double>> u_multi, f_multi;

    data_multiplier(u_in, u_multi, ndata_fit, symmetry);
    data_multiplier(f_in, f_multi, ndata_fit, symmetry);

    const int natmin = symmetry->nat_prim;
    const int natmin3 = 3 * natmin;
    const int nrows = natmin3 * ndata_fit * symmetry->ntran;
    auto ncols = 0;
    auto ncols_new = 0;

    for (i = 0; i < maxorder; ++i) {
        ncols += fcs->nequiv[i].size();
        ncols_new += constraint->index_bimap[i].size();
    }

    const long ncycle = static_cast<long>(ndata_fit) * symmetry->ntran;

    std::vector<double> bvec_orig(nrows, 0.0);


#ifdef _OPENMP
#pragma omp parallel private(irow, i, j)
#endif
    {
        int *ind;
        int mm, order, iat, k;
        int im, iparam;
        long idata;
        int ishift;
        int iold, inew;
        double amat_tmp;
        double **amat_orig_tmp;
        double **amat_mod_tmp;

        allocate(ind, maxorder + 1);
        allocate(amat_orig_tmp, natmin3, ncols);
        allocate(amat_mod_tmp, natmin3, ncols_new);

#ifdef _OPENMP
#pragma omp for schedule(guided)
#endif
        for (irow = 0; irow < ncycle; ++irow) {

            // generate r.h.s vector B
            for (i = 0; i < natmin; ++i) {
                iat = symmetry->map_p2s[i][0];
                for (j = 0; j < 3; ++j) {
                    im = 3 * i + j + natmin3 * irow;
                    bvec[im] = f_multi[irow][3 * iat + j];
                    bvec_orig[im] = f_multi[irow][3 * iat + j];
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    amat_orig_tmp[i][j] = 0.0;
                }
                for (j = 0; j < ncols_new; ++j) {
                    amat_mod_tmp[i][j] = 0.0;
                }
            }

            // generate l.h.s. matrix A

            idata = natmin3 * irow;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                mm = 0;

                for (const auto &iter : fcs->nequiv[order]) {
                    for (i = 0; i < iter; ++i) {
                        ind[0] = fcs->fc_table[order][mm].elems[0];
                        k = inprim_index(ind[0], symmetry);

                        amat_tmp = 1.0;
                        for (j = 1; j < order + 2; ++j) {
                            ind[j] = fcs->fc_table[order][mm].elems[j];
                            amat_tmp *= u_multi[irow][fcs->fc_table[order][mm].elems[j]];
                        }
                        amat_orig_tmp[k][iparam] -= gamma(order + 2, ind)
                            * fcs->fc_table[order][mm].sign * amat_tmp;
                        ++mm;
                    }
                    ++iparam;
                }
            }

            // Convert the full matrix and vector into a smaller irreducible form
            // by using constraint information.

            ishift = 0;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                for (i = 0; i < constraint->const_fix[order].size(); ++i) {

                    for (j = 0; j < natmin3; ++j) {
                        bvec[j + idata] -= constraint->const_fix[order][i].val_to_fix
                            * amat_orig_tmp[j][ishift + constraint->const_fix[order][i].p_index_target];
                    }
                }

                //                std::cout << "pass const_fix" << std::endl;

                for (const auto &it : constraint->index_bimap[order]) {
                    inew = it.left + iparam;
                    iold = it.right + ishift;

                    for (j = 0; j < natmin3; ++j) {
                        amat_mod_tmp[j][inew] = amat_orig_tmp[j][iold];
                    }
                }

                for (i = 0; i < constraint->const_relate[order].size(); ++i) {

                    iold = constraint->const_relate[order][i].p_index_target + ishift;

                    for (j = 0; j < constraint->const_relate[order][i].alpha.size(); ++j) {

                        inew = constraint->index_bimap[order].right.at(
                                constraint->const_relate[order][i].p_index_orig[j]) +
                            iparam;

                        for (k = 0; k < natmin3; ++k) {
                            amat_mod_tmp[k][inew] -= amat_orig_tmp[k][iold]
                                * constraint->const_relate[order][i].alpha[j];
                        }
                    }
                }

                ishift += fcs->nequiv[order].size();
                iparam += constraint->index_bimap[order].size();
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols_new; ++j) {
                    // Transpose here for later use of lapack without transpose
                    amat[natmin3 * ncycle * j + i + idata] = amat_mod_tmp[i][j];
                }
            }
        }

        deallocate(ind);
        deallocate(amat_orig_tmp);
        deallocate(amat_mod_tmp);
    }

    fnorm = 0.0;
    for (i = 0; i < bvec_orig.size(); ++i) {
        fnorm += bvec_orig[i] * bvec_orig[i];
    }
    fnorm = std::sqrt(fnorm);
}

#ifdef WITH_SPARSE_SOLVER
void Fitting::get_matrix_elements_in_sparse_form(const int maxorder,
                                                 const int ndata_fit,
                                                 SpMat &sp_amat,
                                                 Eigen::VectorXd &sp_bvec,
                                                 double &fnorm,
                                                 const Symmetry *symmetry,
                                                 const Fcs *fcs,
                                                 const Constraint *constraint)
{
    int i, j;
    long irow;
    typedef Eigen::Triplet<double> T;
    std::vector<T> nonzero_entries;
    std::vector<std::vector<double>> u_multi, f_multi;

    data_multiplier(u_in, u_multi, ndata_fit, symmetry);
    data_multiplier(f_in, f_multi, ndata_fit, symmetry);

    const int natmin = symmetry->nat_prim;
    const int natmin3 = 3 * natmin;
    const int nrows = natmin3 * ndata_fit * symmetry->ntran;
    auto ncols = 0;
    auto ncols_new = 0;

    for (i = 0; i < maxorder; ++i) {
        ncols += fcs->nequiv[i].size();
        ncols_new += constraint->index_bimap[i].size();
    }

    const long ncycle = static_cast<long>(ndata_fit) * symmetry->ntran;

    std::vector<double> bvec_orig(nrows, 0.0);


#ifdef _OPENMP
#pragma omp parallel private(irow, i, j)
#endif
    {
        int *ind;
        int mm, order, iat, k;
        int im, iparam;
        long idata;
        int ishift;
        int iold, inew;
        double amat_tmp;
        double **amat_orig_tmp;
        double **amat_mod_tmp;

        std::vector<T> nonzero_omp;

        allocate(ind, maxorder + 1);
        allocate(amat_orig_tmp, natmin3, ncols);
        allocate(amat_mod_tmp, natmin3, ncols_new);

#ifdef _OPENMP
#pragma omp for schedule(guided)
#endif
        for (irow = 0; irow < ncycle; ++irow) {

            // generate r.h.s vector B
            for (i = 0; i < natmin; ++i) {
                iat = symmetry->map_p2s[i][0];
                for (j = 0; j < 3; ++j) {
                    im = 3 * i + j + natmin3 * irow;
                    sp_bvec(im) = f_multi[irow][3 * iat + j];
                    bvec_orig[im] = f_multi[irow][3 * iat + j];
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    amat_orig_tmp[i][j] = 0.0;
                }
                for (j = 0; j < ncols_new; ++j) {
                    amat_mod_tmp[i][j] = 0.0;
                }
            }

            // generate l.h.s. matrix A

            idata = natmin3 * irow;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                mm = 0;

                for (const auto &iter : fcs->nequiv[order]) {
                    for (i = 0; i < iter; ++i) {
                        ind[0] = fcs->fc_table[order][mm].elems[0];
                        k = inprim_index(ind[0], symmetry);

                        amat_tmp = 1.0;
                        for (j = 1; j < order + 2; ++j) {
                            ind[j] = fcs->fc_table[order][mm].elems[j];
                            amat_tmp *= u_multi[irow][fcs->fc_table[order][mm].elems[j]];
                        }
                        amat_orig_tmp[k][iparam] -= gamma(order + 2, ind)
                            * fcs->fc_table[order][mm].sign * amat_tmp;
                        ++mm;
                    }
                    ++iparam;
                }
            }

            // Convert the full matrix and vector into a smaller irreducible form
            // by using constraint information.

            ishift = 0;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                for (i = 0; i < constraint->const_fix[order].size(); ++i) {

                    for (j = 0; j < natmin3; ++j) {
                        sp_bvec(j + idata) -= constraint->const_fix[order][i].val_to_fix
                            * amat_orig_tmp[j][ishift + constraint->const_fix[order][i].p_index_target];
                    }
                }

                for (const auto &it : constraint->index_bimap[order]) {
                    inew = it.left + iparam;
                    iold = it.right + ishift;

                    for (j = 0; j < natmin3; ++j) {
                        amat_mod_tmp[j][inew] = amat_orig_tmp[j][iold];
                    }
                }

                for (i = 0; i < constraint->const_relate[order].size(); ++i) {

                    iold = constraint->const_relate[order][i].p_index_target + ishift;

                    for (j = 0; j < constraint->const_relate[order][i].alpha.size(); ++j) {

                        inew = constraint->index_bimap[order].right.at(
                                constraint->const_relate[order][i].p_index_orig[j]) +
                            iparam;

                        for (k = 0; k < natmin3; ++k) {
                            amat_mod_tmp[k][inew] -= amat_orig_tmp[k][iold]
                                * constraint->const_relate[order][i].alpha[j];
                        }
                    }
                }

                ishift += fcs->nequiv[order].size();
                iparam += constraint->index_bimap[order].size();
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols_new; ++j) {
                   if (std::abs(amat_mod_tmp[i][j]) > eps) {
                       nonzero_omp.emplace_back(T(idata + i,j,amat_mod_tmp[i][j]));
                   }
                }
            }
        }

        deallocate(ind);
        deallocate(amat_orig_tmp);
        deallocate(amat_mod_tmp);

#pragma omp critical
        {
            for (const auto &it : nonzero_omp) {
                nonzero_entries.emplace_back(it);
            }
        }
    }

    fnorm = 0.0;
    for (i = 0; i < bvec_orig.size(); ++i) {
        fnorm += bvec_orig[i] * bvec_orig[i];
    }
    fnorm = std::sqrt(fnorm);
    sp_amat.setFromTriplets(nonzero_entries.begin(), nonzero_entries.end());
    sp_amat.makeCompressed();
}
#endif


void Fitting::recover_original_forceconstants(const int maxorder,
                                              const std::vector<double> &param_in,
                                              std::vector<double> &param_out,
                                              std::vector<int> *nequiv,
                                              const Constraint *constraint) const
{
    // Expand the given force constants into the larger sets
    // by using the constraint matrix.

    int i, j, k;
    int ishift = 0;
    int iparam = 0;
    double tmp;
    int inew, iold;

    unsigned int nparams = 0;

    for (i = 0; i < maxorder; ++i) nparams += nequiv[i].size();

    param_out.resize(nparams, 0.0);

    for (i = 0; i < maxorder; ++i) {
        for (j = 0; j < constraint->const_fix[i].size(); ++j) {
            param_out[constraint->const_fix[i][j].p_index_target + ishift]
                = constraint->const_fix[i][j].val_to_fix;
        }

        for (const auto &it : constraint->index_bimap[i]) {
            inew = it.left + iparam;
            iold = it.right + ishift;

            param_out[iold] = param_in[inew];
        }

        for (j = 0; j < constraint->const_relate[i].size(); ++j) {
            tmp = 0.0;

            for (k = 0; k < constraint->const_relate[i][j].alpha.size(); ++k) {
                tmp += constraint->const_relate[i][j].alpha[k]
                    * param_out[constraint->const_relate[i][j].p_index_orig[k] + ishift];
            }
            param_out[constraint->const_relate[i][j].p_index_target + ishift] = -tmp;
        }

        ishift += nequiv[i].size();
        iparam += constraint->index_bimap[i].size();
    }
}


void Fitting::data_multiplier(double **data_in,
                              std::vector<std::vector<double>> &data_out,
                              const int ndata_used,
                              const Symmetry *symmetry) const
{
    int i, j, k;
    int n_mapped;

    const int nat = symmetry->nat_prim * symmetry->ntran;

    auto idata = 0;
    for (i = 0; i < ndata_used; ++i) {
        std::vector<double> data_tmp(3 * nat, 0.0);

        for (int itran = 0; itran < symmetry->ntran; ++itran) {
            for (j = 0; j < nat; ++j) {
                n_mapped = symmetry->map_sym[j][symmetry->symnum_tran[itran]];
                for (k = 0; k < 3; ++k) {
                    data_tmp[3 * n_mapped + k] = data_in[i][3 * j + k];
                }
            }
            data_out.emplace_back(data_tmp);
            ++idata;
        }
    }
}

int Fitting::inprim_index(const int n,
                          const Symmetry *symmetry) const
{
    int in = -1;
    const auto atmn = n / 3;
    const auto crdn = n % 3;

    for (int i = 0; i < symmetry->nat_prim; ++i) {
        if (symmetry->map_p2s[i][0] == atmn) {
            in = 3 * i + crdn;
            break;
        }
    }
    return in;
}

double Fitting::gamma(const int n,
                      const int *arr) const
{
    int *arr_tmp, *nsame;
    int i;

    allocate(arr_tmp, n);
    allocate(nsame, n);

    for (i = 0; i < n; ++i) {
        arr_tmp[i] = arr[i];
        nsame[i] = 0;
    }

    const auto ind_front = arr[0];
    auto nsame_to_front = 1;

    insort(n, arr_tmp);

    auto nuniq = 1;
    auto iuniq = 0;

    nsame[0] = 1;

    for (i = 1; i < n; ++i) {
        if (arr_tmp[i] == arr_tmp[i - 1]) {
            ++nsame[iuniq];
        } else {
            ++nsame[++iuniq];
            ++nuniq;
        }

        if (arr[i] == ind_front) ++nsame_to_front;
    }

    auto denom = 1;

    for (i = 0; i < nuniq; ++i) {
        denom *= factorial(nsame[i]);
    }

    deallocate(arr_tmp);
    deallocate(nsame);

    return static_cast<double>(nsame_to_front) / static_cast<double>(denom);
}

int Fitting::factorial(const int n) const
{
    if (n == 1 || n == 0) {
        return 1;
    }
    return n * factorial(n - 1);
}


int Fitting::rankQRD(const int m,
                     const int n,
                     double *mat,
                     const double tolerance) const
{
    // Return the rank of matrix mat revealed by the column pivoting QR decomposition
    // The matrix mat is destroyed.

    auto m_ = m;
    auto n_ = n;

    auto LDA = m_;

    auto LWORK = 10 * n_;
    int INFO;
    int *JPVT;
    double *WORK, *TAU;

    const auto nmin = std::min<int>(m_, n_);

    allocate(JPVT, n_);
    allocate(WORK, LWORK);
    allocate(TAU, nmin);

    for (int i = 0; i < n_; ++i) JPVT[i] = 0;

    dgeqp3_(&m_, &n_, mat, &LDA, JPVT, TAU, WORK, &LWORK, &INFO);

    deallocate(JPVT);
    deallocate(WORK);
    deallocate(TAU);

    if (std::abs(mat[0]) < eps) return 0;

    double **mat_tmp;
    allocate(mat_tmp, m_, n_);

    unsigned long k = 0;

    for (int j = 0; j < n_; ++j) {
        for (int i = 0; i < m_; ++i) {
            mat_tmp[i][j] = mat[k++];
        }
    }

    auto nrank = 0;
    for (int i = 0; i < nmin; ++i) {
        if (std::abs(mat_tmp[i][i]) > tolerance * std::abs(mat[0])) ++nrank;
    }

    deallocate(mat_tmp);

    return nrank;
}


#ifdef WITH_SPARSE_SOLVER
int Fitting::run_eigen_sparseQR(const SpMat &sp_mat,
                                const Eigen::VectorXd &sp_bvec,
                                std::vector<double> &param_out,
                                const double fnorm,
                                const int maxorder,
                                const Fcs *fcs,
                                const Constraint *constraint,
                                const int verbosity)
{
//    Eigen::BenchTimer t;

    SpMat AtA;

//    std::cout << "Start calculating AtA ..." << std::endl;
    AtA = sp_mat.transpose()*sp_mat;
//    std::cout << sp_mat.rows() << "x" << sp_mat.cols() << "\n";

//    std::cout << "done." << std::endl;

//   std::cout << "Start calculating AtB ..." << std::endl;
 //   Eigen::VectorXd AtB, x;
    auto AtB = sp_mat.transpose()*sp_bvec;
 //   std::cout << "done." << std::endl;

    if (verbosity > 0) {
        std::cout << "  Solve least-squares problem by sparse LDLT." << std::endl;
    }


    // t.reset(); t.start();
// Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> qr(sp_mat);
// Eigen::VectorXd x = qr.solve(sp_bvec);

// t.stop();
// std::cout << "sqr   : " << qr.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";

//    t.reset(); t.start();
    Eigen::SimplicialLDLT<SpMat> ldlt(AtA);
 //   x.setZero();
    auto x = ldlt.solve(AtB);
//    t.stop();
//std::cout << "ldlt  : " << ldlt.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";


// t.reset(); t.start();
// Eigen::ConjugateGradient<SpMat> cg(AtA);
// cg.setTolerance(eps10);
// cg.setMaxIterations(10000000);
// x.setZero(); x = cg.solve(AtB);
// t.stop();
// std::cout << "cg    : " << cg.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";

// t.reset(); t.start();
// Eigen::LeastSquaresConjugateGradient<SpMat> lscg(sp_mat);
// lscg.setTolerance(eps10);
// lscg.setMaxIterations(10000000);
// x.setZero(); x = lscg.solve(sp_bvec);

// t.stop();
// std::cout << "lscg  : " << lscg.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";

// t.reset(); t.start();
// Eigen::BiCGSTAB<SpMat> bicg(AtA);
// bicg.setTolerance(eps10);
// bicg.setMaxIterations(10000000);
// x.setZero(); x = bicg.solve(AtB);
// t.stop();
    // std::cout << "bicg    : " << bicg.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";


    auto res = sp_bvec - sp_mat * x;
    auto res2norm = res.squaredNorm();
    auto nparams = x.size();
    std::vector<double> param_irred(nparams);

    for (auto i = 0; i < nparams; ++i) {
        param_irred[i] = x(i);
    }

    if (ldlt.info() == Eigen::Success) {
    // Recover reducible set of force constants

        recover_original_forceconstants(maxorder,
                                        param_irred,
                                        param_out,
                                        fcs->nequiv,
                                        constraint);

        if (verbosity > 0) {
            std::cout << "  Residual sum of squares for the solution: "
                << sqrt(res2norm) << std::endl;
            std::cout << "  Fitting error (%) : "
                << sqrt(res2norm / (fnorm * fnorm)) * 100.0 << std::endl;
        }

        return 0;

    } else {

        std::cerr << "  Fitting by LDLT failed." << std::endl;
        std::cerr << ldlt.info() << std::endl;

        return 1;
    }

}

#endif


void Fitting::lasso_main(const Symmetry *symmetry,
                         const Interaction *interaction,
                         const Fcs *fcs,
                         const Constraint *constraint,
                         const unsigned int nat,
                         const Files *files,
                         const int verbosity,
                         Fitting *fitting,
                         Timer *timer)
{
    int i, j, k;

    const auto natmin = symmetry->nat_prim;
    const int maxorder = interaction->maxorder;
    const int ndata = fitting->ndata;
    const int nstart = fitting->nstart;
    const int nend = fitting->nend;
    const int skip_s = fitting->skip_s;
    const int skip_e = fitting->skip_e;
    const int ntran = symmetry->ntran;
    const int ndata_used = nend - nstart + 1 - skip_e + skip_s;

    double scale_factor;
    double **u, **f;

    double *bvec_breg, *dvec_breg;
    // for LASSO validation
    double **u_test, **f_test;

    double fnorm, fnorm_test;
    int ndata_used_test = nend_test - nstart_test + 1;

    int N = 0;
    int N_new = 0;
    for (i = 0; i < maxorder; ++i) {
        N += fcs->nequiv[i].size();
        N_new += constraint->index_bimap[i].size();
    }

    int M = 3 * natmin * static_cast<long>(ndata_used) * ntran;
    int M_test = 3 * natmin * ndata_used_test * ntran;

    if (verbosity > 0) {
        std::cout << " LASSO" << std::endl;
        std::cout << " =====" << std::endl << std::endl;

        std::cout << "  Reference files" << std::endl;
        std::cout << "   Displacement: " << files->file_disp << std::endl;
        std::cout << "   Force       : " << files->file_force << std::endl;
        std::cout << std::endl;

        std::cout << "  NSTART = " << nstart << "; NEND = " << nend;
        if (skip_s < skip_e) std::cout << ": SKIP = " << skip_s + 1 << "-" << skip_e;
        std::cout << std::endl;
        std::cout << "  " << ndata_used
            << " entries will be used for lasso." << std::endl << std::endl;

        std::cout << "  Validation test files" << std::endl;
        std::cout << "   Displacement: " << dfile_test << std::endl;
        std::cout << "   Force       : " << ffile_test << std::endl;
        std::cout << std::endl;

        std::cout << "  NSTART = " << nstart_test << "; NEND = " << nend_test << std::endl;
        std::cout << "  " << ndata_used_test
            << " entries will be used for lasso validation." << std::endl << std::endl;

        std::cout << "  Total Number of Parameters : " << N << std::endl;
        std::cout << "  Total Number of Free Parameters : " << N_new << std::endl << std::endl;
    }

    // Parse displacement and force
    allocate(u, ndata_used, 3 * nat);
    allocate(f, ndata_used, 3 * nat);
    allocate(u_test, ndata_used_test, 3 * nat);
    allocate(f_test, ndata_used_test, 3 * nat);

    InputParser *input_parser = new InputParser();

    input_parser->parse_displacement_and_force_files(u,
                                                     f,
                                                     nat,
                                                     ndata,
                                                     nstart,
                                                     nend,
                                                     skip_s,
                                                     skip_e,
                                                     files->file_disp,
                                                     files->file_force);

    input_parser->parse_displacement_and_force_files(u_test,
                                                     f_test,
                                                     nat,
                                                     ndata_test,
                                                     nstart_test,
                                                     nend_test,
                                                     0,
                                                     0,
                                                     dfile_test,
                                                     ffile_test);

    delete input_parser;

    // Scale displacements

    const double inv_dnorm = 1.0 / disp_norm;
    for (i = 0; i < ndata_used; ++i) {
        for (j = 0; j < 3 * nat; ++j) {
            u[i][j] *= inv_dnorm;
        }
    }

    for (i = 0; i < ndata_used_test; ++i) {
        for (j = 0; j < 3 * nat; ++j) {
            u_test[i][j] *= inv_dnorm;
        }
    }

    // Scale force constants
    for (i = 0; i < maxorder; ++i) {
        scale_factor = std::pow(disp_norm, i + 1);
        for (j = 0; j < constraint->const_fix[i].size(); ++j) {
            constraint->const_fix[i][j].val_to_fix *= scale_factor;
        }
    }


    unsigned long nrows = 3 * static_cast<long>(natmin)
        * static_cast<long>(ndata_used)
        * static_cast<long>(ntran);

    const unsigned long ncols = static_cast<long>(N_new);

    std::vector<double> amat_1D, amat_1D_test;
    std::vector<double> bvec, bvec_test;

    amat_1D.resize(nrows * ncols, 0.0);
    bvec.resize(nrows, 0.0);

    fitting->set_displacement_and_force(u, f, nat, ndata_used);

    fitting->get_matrix_elements_algebraic_constraint(maxorder,
                                                      ndata_used,
                                                      &amat_1D[0],
                                                      &bvec[0],
                                                      fnorm,
                                                      symmetry,
                                                      fcs,
                                                      constraint);

    deallocate(u);
    deallocate(f);

    nrows = 3 * static_cast<long>(natmin)
        * static_cast<long>(ndata_used_test)
        * static_cast<long>(ntran);

    amat_1D_test.resize(nrows * ncols, 0.0);
    bvec_test.resize(nrows, 0.0);

    fitting->set_displacement_and_force(u_test, f_test, nat, ndata_used_test);

    fitting->get_matrix_elements_algebraic_constraint(maxorder,
                                                      ndata_used_test,
                                                      &amat_1D_test[0],
                                                      &bvec_test[0],
                                                      fnorm_test,
                                                      symmetry,
                                                      fcs,
                                                      constraint);

    deallocate(u_test);
    deallocate(f_test);

    // Scale back force constants

    for (i = 0; i < maxorder; ++i) {
        scale_factor = 1.0 / std::pow(disp_norm, i + 1);
        for (j = 0; j < constraint->const_fix[i].size(); ++j) {
            constraint->const_fix[i][j].val_to_fix *= scale_factor;
        }
    }

    // Start Lasso optimization

    std::vector<double> param(N_new);

    double *factor_std;
    bool *has_prod;

    Eigen::MatrixXd A, Prod;
    Eigen::VectorXd b, C, grad, x;
    Eigen::VectorXd scale_beta;
    Eigen::MatrixXd A_test;
    Eigen::VectorXd b_test;
    Eigen::VectorXd fdiff, fdiff_test;

    double **amat = nullptr;
    double *fsum = nullptr;
    double **amat_test = nullptr;
    double *fsum_test = nullptr;


    // Coordinate descent

    A = Eigen::Map<Eigen::MatrixXd>(&amat_1D[0], M, N_new);
    b = Eigen::Map<Eigen::VectorXd>(&bvec[0], M);
    A_test = Eigen::Map<Eigen::MatrixXd>(&amat_1D_test[0], M_test, N_new);
    b_test = Eigen::Map<Eigen::VectorXd>(&bvec_test[0], M_test);

    Prod.resize(N_new, N_new);
    C.resize(N_new);
    grad.resize(N_new);
    x.resize(N_new);
    scale_beta.resize(N_new);
    fdiff.resize(M);
    fdiff_test.resize(M);

    allocate(has_prod, N_new);
    allocate(factor_std, N_new);

    for (i = 0; i < N_new; ++i) {
        param[i] = 0.0;
        x[i] = 0.0;
        has_prod[i] = false;
    }

    // Standardize if necessary

    double Minv = 1.0 / static_cast<double>(M);

    if (standardize) {
        double sum1, sum2;

        std::cout << " STANDARDIZE = 1 : Standardization will be performed for matrix A and vector b." << std::endl;
        std::cout << "                   The LASSO_DNORM-tag will be neglected." << std::endl;
        for (j = 0; j < N_new; ++j) {
            sum1 = A.col(j).sum() * Minv;
            sum2 = A.col(j).dot(A.col(j)) * Minv;

            for (i = 0; i < M; ++i) {
                A(i, j) = (A(i, j) - sum1) / std::sqrt(sum2 - sum1 * sum1);
            }
            for (i = 0; i < M_test; ++i) {
                A_test(i, j) = (A_test(i, j) - sum1) / std::sqrt(sum2 - sum1 * sum1);
            }
            factor_std[j] = 1.0 / std::sqrt(sum2 - sum1 * sum1);
            scale_beta(j) = 1.0;
        }

    } else {
        double sum2;
        std::cout << " STANDARDIZE = 0 : No standardization of matrix A and vector b." << std::endl;
        std::cout << "                   Columns of matrix A will be scaled by the LASSO_DNORM value." << std::endl;
        for (j = 0; j < N_new; ++j) {
            factor_std[j] = 1.0;
            sum2 = A.col(j).dot(A.col(j)) * Minv;
            scale_beta(j) = 1.0 / sum2;
        }
    }

    C = A.transpose() * b;
    auto lambda_max = 0.0;
    for (i = 0; i < N_new; ++i) {
        lambda_max = std::max<double>(lambda_max, std::abs(C(i)));
    }
    lambda_max /= static_cast<double>(M);
    std::cout << std::endl;
    std::cout << " Recommended LASSO_MAXALPHA = " << lambda_max << std::endl << std::endl;
    grad = C;
    for (i = 0; i < N_new; ++i) {
        for (j = 0; j < N_new; ++j) {
            Prod(i, j) = 0.0;
        }
    }

    if (lasso_cv == 1) {

        // Cross-validation mode

        std::cout << "  Lasso validation with the following parameters:" << std::endl;
        std::cout << "   LASSO_MINALPHA = " << std::setw(15) << l1_alpha_min;
        std::cout << " LASSO_MAXALPHA = " << std::setw(15) << l1_alpha_max << std::endl;
        std::cout << "   LASSO_NALPHA = " << std::setw(5) << num_l1_alpha << std::endl;
        std::cout << "   LASSO_TOL = " << std::setw(15) << lasso_tol << std::endl;
        std::cout << "   LASSO_MAXITER = " << std::setw(5) << maxiter << std::endl;
        std::cout << "   LASSO_DBASIS = " << std::setw(15) << disp_norm << std::endl;
        std::cout << std::endl;

        std::ofstream ofs_cv, ofs_coef;

        std::string file_cv = files->job_title + ".lasso_cv";
        std::string file_coef = files->job_title + ".lasso_coef";
        ofs_cv.open(file_cv.c_str(), std::ios::out);

        ofs_cv << "# Algorithm : Coordinate descent" << std::endl;
        ofs_cv << "# LASSO_DBASIS = " << std::setw(15) << disp_norm << std::endl;
        ofs_cv << "# LASSO_TOL = " << std::setw(15) << lasso_tol << std::endl;
        ofs_cv << "# L1 ALPHA, Fitting error, Validation error, Num. zero IFCs (2nd, 3rd, ...) " << std::endl;


        int initialize_mode;
        double res1, res2;
        std::vector<int> nzero_lasso(maxorder);
        std::vector<double> params_tmp;

        if (save_solution_path) {
            ofs_coef.open(file_coef.c_str(), std::ios::out);
            ofs_coef << "# L1 ALPHA, coefficients" << std::endl;
            params_tmp.resize(N_new);
        }


        for (int ialpha = 0; ialpha <= num_l1_alpha; ++ialpha) {

            l1_alpha = l1_alpha_min * std::pow(l1_alpha_max / l1_alpha_min,
                                               static_cast<double>(num_l1_alpha - ialpha) / static_cast<double>(
                                                   num_l1_alpha));

            std::cout << "-----------------------------------------------------------------" << std::endl;
            std::cout << "  L1_ALPHA = " << std::setw(15) << l1_alpha << std::endl;

            ofs_cv << std::setw(15) << l1_alpha;

            if (ialpha == 0) {
                initialize_mode = 0;
            } else {
                initialize_mode = 1;
            }

            coordinate_descent(M, N_new, l1_alpha, lasso_tol, initialize_mode, maxiter,
                               x, A, b, C, has_prod, Prod, grad, fnorm, output_frequency,
                               scale_beta, standardize);

            for (i = 0; i < N_new; ++i) param[i] = x[i];

            fdiff = A * x - b;
            fdiff_test = A_test * x - b_test;
            res1 = fdiff.dot(fdiff) / (fnorm * fnorm);
            res2 = fdiff_test.dot(fdiff_test) / (fnorm_test * fnorm_test);

            // Count the number of zero parameters
            int iparam = 0;

            for (i = 0; i < maxorder; ++i) {
                nzero_lasso[i] = 0;
                for (const auto &it : constraint->index_bimap[i]) {
                    int inew = it.left + iparam;
                    if (std::abs(param[inew]) < eps) ++nzero_lasso[i];

                }
                iparam += constraint->index_bimap[i].size();
            }

            ofs_cv << std::setw(15) << std::sqrt(res1);
            ofs_cv << std::setw(15) << std::sqrt(res2);
            for (i = 0; i < maxorder; ++i) {
                ofs_cv << std::setw(10) << nzero_lasso[i];
            }
            ofs_cv << std::endl;

            if (save_solution_path) {
                ofs_coef << std::setw(15) << l1_alpha;

                for (i = 0; i < N_new; ++i) params_tmp[i] = param[i];
                k = 0;
                for (i = 0; i < maxorder; ++i) {
                    scale_factor = 1.0 / std::pow(disp_norm, i + 1);

                    for (j = 0; j < constraint->index_bimap[i].size(); ++j) {
                        params_tmp[k] *= scale_factor * factor_std[k];
                        ++k;
                    }
                }
                for (i = 0; i < N_new; ++i) {
                    ofs_coef << std::setw(15) << params_tmp[i];
                }
                ofs_coef << std::endl;
            }
        }
        if (save_solution_path) ofs_coef.close();
        ofs_cv.close();

    } else if (lasso_cv == 0) {

        double res1;
        std::vector<int> nzero_lasso(maxorder);

        std::cout << "  Lasso minimization with the following parameters:" << std::endl;
        std::cout << "   LASSO_ALPHA  (L1) = " << std::setw(15) << l1_alpha << std::endl;
        std::cout << "   LASSO_TOL = " << std::setw(15) << lasso_tol << std::endl;
        std::cout << "   LASSO_MAXITER = " << std::setw(5) << maxiter << std::endl;
        std::cout << "   LASSO_DBASIS = " << std::setw(15) << disp_norm << std::endl;

        std::cout << std::endl;

        // Coordinate Descent Method
        coordinate_descent(M, N_new, l1_alpha, lasso_tol, 0, maxiter,
                           x, A, b, C, has_prod, Prod, grad, fnorm,
                           output_frequency, scale_beta, standardize);

        for (i = 0; i < N_new; ++i) param[i] = x[i] * factor_std[i];

        fdiff = A * x - b;
        res1 = fdiff.dot(fdiff) / (fnorm * fnorm);

        // Count the number of zero parameters
        int iparam, inew;
        iparam = 0;

        for (i = 0; i < maxorder; ++i) {
            nzero_lasso[i] = 0;
            for (boost::bimap<int, int>::const_iterator it = constraint->index_bimap[i].begin();
                 it != constraint->index_bimap[i].end(); ++it) {
                inew = (*it).left + iparam;
                if (std::abs(param[inew]) < eps) ++nzero_lasso[i];

            }
            iparam += constraint->index_bimap[i].size();
        }

        std::cout << "  RESIDUAL (%): " << std::sqrt(res1) * 100.0 << std::endl;
        for (int order = 0; order < maxorder; ++order) {
            std::cout << "  Number of non-zero " << std::setw(9) << interaction->str_order[order] << " FCs : "
                << constraint->index_bimap[order].size() - nzero_lasso[order] << std::endl;
        }
        std::cout << std::endl;
    }


    if (debias_ols) {
        // Perform OLS fitting to the features selected by LASSO for reducing the bias.

        std::cout << " DEBIAS_OLS = 1: Attempt to reduce the bias of LASSO by performing OLS fitting" << std::endl;
        std::cout << "                 with features selected by LASSO." << std::endl;

        std::vector<int> nonzero_index, zero_index;

        for (i = 0; i < N_new; ++i) {
            if (std::abs(param[i]) >= eps) {
                nonzero_index.push_back(i);
            } else {
                zero_index.push_back(i);
            }
        }

        int N_nonzero = nonzero_index.size();
        Eigen::MatrixXd A_nonzero(M, N_nonzero);

        for (i = 0; i < N_nonzero; ++i) {
            A_nonzero.col(i) = A.col(nonzero_index[i]);
        }
        Eigen::VectorXd x_nonzero = A_nonzero.colPivHouseholderQr().solve(b);

        for (i = 0; i < N_new; ++i) param[i] = 0.0;
        for (i = 0; i < N_nonzero; ++i) {
            param[nonzero_index[i]] = x_nonzero[i] * factor_std[nonzero_index[i]];
        }
    }

    k = 0;
    for (i = 0; i < maxorder; ++i) {
        scale_factor = 1.0 / std::pow(disp_norm, i + 1);

        for (j = 0; j < constraint->index_bimap[i].size(); ++j) {
            param[k] *= scale_factor;
            ++k;
        }
    }

    deallocate(has_prod);
    deallocate(factor_std);

    fitting->set_fcs_values(maxorder,
                            &param[0],
                            fcs->nequiv,
                            constraint);

    if (amat) {
        deallocate(amat);
    }
    if (fsum) {
        deallocate(fsum);
    }
    if (amat_test) {
        deallocate(amat_test);
    }
    if (fsum_test) {
        deallocate(fsum_test);
    }

    timer->print_elapsed();
    std::cout << " --------------------------------------------------------------" << std::endl;
}


void Fitting::calculate_residual(const int M,
                                 const int N,
                                 double **Amat,
                                 double *param,
                                 double *fvec,
                                 const double fnorm,
                                 double &res) const
{
    int i, j;
    using namespace Eigen;

    MatrixXd Amat2(M, N);
    VectorXd param2(N);
    VectorXd fvec2(M), vec_tmp(M);

    for (i = 0; i < M; ++i) {
        for (j = 0; j < N; ++j) {
            Amat2(i, j) = Amat[i][j];
        }
        fvec2(i) = fvec[i];
    }

    for (i = 0; i < N; ++i) {
        param2(i) = param[i];
    }

    vec_tmp = Amat2 * param2 - fvec2;
    res = vec_tmp.dot(vec_tmp) / (fnorm * fnorm);
}

void Fitting::coordinate_descent(const int M,
                                 const int N,
                                 const double alpha,
                                 const double tolerance,
                                 const int warm_start,
                                 const int maxiter,
                                 Eigen::VectorXd &x,
                                 const Eigen::MatrixXd &A,
                                 const Eigen::VectorXd &b,
                                 const Eigen::VectorXd &C,
                                 bool *has_prod,
                                 Eigen::MatrixXd &Prod,
                                 Eigen::VectorXd &grad,
                                 const double fnorm,
                                 const int nfreq,
                                 Eigen::VectorXd scale_beta,
                                 const int standardize) const
{
    int i, j;
    int iloop;
    double diff;
    Eigen::VectorXd beta(N), delta(N);
    Eigen::VectorXd res(N);
    bool do_print_log;

    if (warm_start) {
        for (i = 0; i < N; ++i) beta(i) = x[i];
    } else {
        for (i = 0; i < N; ++i) beta(i) = 0.0;
        grad = C;
    }

    double Minv = 1.0 / static_cast<double>(M);

    iloop = 0;
    if (standardize) {
        while (iloop < maxiter) {
            do_print_log = !((iloop + 1) % nfreq);

            if (do_print_log) {
                std::cout << "   Coordinate Descent : " << std::setw(5) << iloop + 1 << std::endl;
            }
            delta = beta;
            for (i = 0; i < N; ++i) {
                beta(i) = shrink(Minv * grad(i) + beta(i), alpha);
                delta(i) -= beta(i);
                if (std::abs(delta(i)) > 0.0) {
                    if (!has_prod[i]) {
                        for (j = 0; j < N; ++j) {
                            Prod(j, i) = A.col(j).dot(A.col(i));
                        }
                        has_prod[i] = true;
                    }
                    grad = grad + Prod.col(i) * delta(i);
                }
            }
            ++iloop;
            diff = std::sqrt(delta.dot(delta) / static_cast<double>(N));

            if (diff < tolerance) break;

            if (do_print_log) {
                double param2norm = beta.dot(beta);
                std::cout << "    1: ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << diff
                    << std::setw(15) << diff * std::sqrt(static_cast<double>(N) / param2norm) << std::endl;
                double tmp = 0.0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+:tmp)
#endif
                for (i = 0; i < N; ++i) {
                    tmp += std::abs(beta(i));
                }
                std::cout << "    2: ||u_{k}||_1             = " << std::setw(15) << tmp << std::endl;
                res = A * beta - b;
                tmp = res.dot(res);
                std::cout << "    3: ||Au_{k}-f||_2          = " << std::setw(15) << std::sqrt(tmp)
                    << std::setw(15) << std::sqrt(tmp / (fnorm * fnorm)) << std::endl;
                std::cout << std::endl;
            }
        }
    } else {
        // Non-standardized version. Needs additional operations
        while (iloop < maxiter) {
            do_print_log = !((iloop + 1) % nfreq);

            if (do_print_log) {
                std::cout << "   Coordinate Descent : " << std::setw(5) << iloop + 1 << std::endl;
            }
            delta = beta;
            for (i = 0; i < N; ++i) {
                beta(i) = shrink(Minv * grad(i) + beta(i) / scale_beta(i), alpha) * scale_beta(i);
                delta(i) -= beta(i);
                if (std::abs(delta(i)) > 0.0) {
                    if (!has_prod[i]) {
                        for (j = 0; j < N; ++j) {
                            Prod(j, i) = A.col(j).dot(A.col(i));
                        }
                        has_prod[i] = true;
                    }
                    grad = grad + Prod.col(i) * delta(i);
                }
            }
            ++iloop;
            diff = std::sqrt(delta.dot(delta) / static_cast<double>(N));

            if (diff < tolerance) break;

            if (do_print_log) {
                double param2norm = beta.dot(beta);
                std::cout << "    1: ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << diff
                    << std::setw(15) << diff * std::sqrt(static_cast<double>(N) / param2norm) << std::endl;
                double tmp = 0.0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+:tmp)
#endif
                for (i = 0; i < N; ++i) {
                    tmp += std::abs(beta(i));
                }
                std::cout << "    2: ||u_{k}||_1             = " << std::setw(15) << tmp << std::endl;
                res = A * beta - b;
                tmp = res.dot(res);
                std::cout << "    3: ||Au_{k}-f||_2          = " << std::setw(15) << std::sqrt(tmp)
                    << std::setw(15) << std::sqrt(tmp / (fnorm * fnorm)) << std::endl;
                std::cout << std::endl;
            }
        }
    }


    if (iloop >= maxiter) {
        std::cout << "WARNING: Convergence NOT achieved within " << maxiter
            << " coordinate descent iterations." << std::endl;
    } else {
        std::cout << "  Convergence achieved in " << iloop << " iterations." << std::endl;
    }

    double param2norm = beta.dot(beta);
    if (std::abs(param2norm) < eps) {
        std::cout << "    1': ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << 0.0
            << std::setw(15) << 0.0 << std::endl;
    } else {
        std::cout << "    1': ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << diff
            << std::setw(15) << diff * std::sqrt(static_cast<double>(N) / param2norm) << std::endl;
    }

    double tmp = 0.0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+:tmp)
#endif
    for (i = 0; i < N; ++i) {
        tmp += std::abs(beta(i));
    }
    std::cout << "    2': ||u_{k}||_1             = " << std::setw(15) << tmp << std::endl;
    res = A * beta - b;
    tmp = res.dot(res);
    std::cout << "    3': ||Au_{k}-f||_2          = " << std::setw(15) << std::sqrt(tmp)
        << std::setw(15) << std::sqrt(tmp / (fnorm * fnorm)) << std::endl;
    std::cout << std::endl;

    for (i = 0; i < N; ++i) x[i] = beta(i);
}


void Fitting::get_prefactor_force(const int maxorder,
                                  const Fcs *fcs,
                                  const Constraint *constraint,
                                  const Fitting *fitting,
                                  std::vector<double> &prefactor) const
{
    int i, j;
    int ishift2 = 0;
    int iparam2 = 0;
    int inew2, iold2;
    int iold2_dup;

    int *ind;

    allocate(ind, maxorder + 1);
    for (i = 0; i < maxorder; ++i) {
        for (const auto &it : constraint->index_bimap[i]) {
            inew2 = it.left + iparam2;
            iold2 = it.right;
            iold2_dup = 0;
            for (j = 0; j < iold2; ++j) {
                iold2_dup += fcs->nequiv[i][j];
            }

            for (j = 0; j < i + 2; ++j) {
                ind[j] = fcs->fc_table[i][iold2_dup].elems[j];
            }
            prefactor[inew2] = fitting->gamma(i + 2, ind);
        }

        ishift2 += fcs->nequiv[i].size();
        iparam2 += constraint->index_bimap[i].size();
    }
    deallocate(ind);
}
