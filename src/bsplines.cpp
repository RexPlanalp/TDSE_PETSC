#include "bsplines.h"
#include "simulation.h"
#include "gauss.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <functional>
#include <algorithm> 

namespace bsplines{



std::complex<double> B(int i, int degree, std::complex<double> x, const simulation& sim)
{
    if (degree == 0)
    {
        return (sim.complex_knots[i].real() <= x.real() && x.real() < sim.complex_knots[i + 1].real() ? 1.0 : 0.0);
    }

    std::complex<double> denom1 = sim.complex_knots[i + degree] - sim.complex_knots[i];
    std::complex<double> denom2 = sim.complex_knots[i + degree + 1] - sim.complex_knots[i + 1];

    std::complex<double> term1 = 0.0;
    std::complex<double> term2 = 0.0;

    if (denom1.real() > 0)
    {
        term1 = (x - sim.complex_knots[i]) / denom1 * B(i, degree - 1, x, sim);
    }
    if (denom2.real() > 0)
    {
        term2 = (sim.complex_knots[i + degree + 1] - x) / denom2 * B(i + 1, degree - 1, x, sim);
    }

    return term1 + term2;
}

std::complex<double> dB(int i, int degree, std::complex<double> x, const simulation& sim)
{
    if (degree == 0)
    {
        return 0.0;
    }

    std::complex<double> denom1 = sim.complex_knots[i + degree] - sim.complex_knots[i];
    std::complex<double> denom2 = sim.complex_knots[i + degree + 1] - sim.complex_knots[i + 1];

    std::complex<double> term1 = 0.0;
    std::complex<double> term2 = 0.0;

    if (denom1.real() > 0)
    {
        term1 = std::complex<double>(degree) / denom1 * B(i, degree - 1, x, sim);
    }
    if (denom2.real() > 0)
    {
        term2 = -std::complex<double>(degree) / denom2 * B(i + 1, degree - 1, x, sim);
    }

    return term1 + term2;
}

void save_debug_bsplines(int rank, const simulation& sim)
{
    if (!sim.misc_data.value("debug", 0)) return; // Only save if debugging is enabled

    if (rank == 0)
    {
        auto space_range = sim.grid_data.value("space", std::vector<double>{0.0, 0, 0.0});
        double rmin = space_range[0];
        double rmax = space_range[1];
        double dr = space_range[2];

        std::ofstream file1("bsplines.txt");
        if (!file1.is_open())
        {
            std::cerr << "Error: could not open file bsplines.txt" << std::endl;
            return;
        }

        for (int i = 0; i < sim.bspline_data.value("n_basis", 0); i++)
        {
            for (int idx = 0; idx < sim.grid_data.value("Nr", 0); ++idx)
            {
                double x_val = rmin + idx * dr;
                std::complex<double> x = sim.ecs_x(x_val);
                std::complex<double> val = B(i, sim.bspline_data.value("degree", 0), x, sim);
                file1 << val.real() << "\t" << val.imag() << "\n";
            }
            file1 << "\n";
        }
        file1.close();
    }
}

std::complex<double> integrate_matrix_element(int i, int j,std::function<std::complex<double>(int, int, std::complex<double>, const simulation&)> integrand,const simulation& sim)
{
    std::complex<double> total = 0.0;
    int lower = std::min(i, j);
    int upper = std::max(i, j);

    std::vector<double> roots = gauss::get_roots(sim.bspline_data.value("order", 0));
    std::vector<double> weights = gauss::get_weights(sim.bspline_data.value("order", 0));

    for (int k = lower; k <= upper + sim.bspline_data.value("degree", 0); ++k)
    {
        double a = sim.knots[k];
        double b = sim.knots[k + 1];

        if (a == b)
        {
            continue;
        }

     


        double half_b_minus_a = 0.5 * (b - a);
        double half_b_plus_a = 0.5 * (b + a);

        for (size_t r = 0; r < roots.size(); ++r)
        {
            

            double x_val = half_b_minus_a * roots[r] + half_b_plus_a;
            double weight_val = weights[r];

            

            

            std::complex<double> x = sim.ecs_x(x_val);
            std::complex<double> weight = sim.ecs_w(x_val, weight_val) * half_b_minus_a;
            total += weight * integrand(i, j, x, sim);  // Direct function call
        }
    }

    return total;
}

std::complex<double> overlap_integrand(int i, int j, std::complex<double> x, const simulation& sim)
{
    return bsplines::B(i, sim.bspline_data.value("degree", 0), x, sim) * 
           bsplines::B(j, sim.bspline_data.value("degree", 0), x, sim);
}

std::complex<double> kinetic_integrand(int i, int j, std::complex<double> x, const simulation& sim)
{
    return 0.5*bsplines::dB(i, sim.bspline_data.value("degree", 0), x, sim) * 
           bsplines::dB(j, sim.bspline_data.value("degree", 0), x, sim);
}

std::complex<double> invr_integrand(int i, int j, std::complex<double> x, const simulation& sim)
{
    return bsplines::B(i, sim.bspline_data.value("degree", 0), x, sim) * 
           bsplines::B(j, sim.bspline_data.value("degree", 0), x, sim) /(x + 1E-25);
}

std::complex<double> invr2_integrand(int i, int j, std::complex<double> x, const simulation& sim)
{
    return bsplines::B(i, sim.bspline_data.value("degree", 0), x, sim) * 
           bsplines::B(j, sim.bspline_data.value("degree", 0), x, sim) /(x*x + 1E-25);
}

std::complex<double> der_integrand(int i, int j, std::complex<double> x, const simulation& sim)
{
    return bsplines::B(i, sim.bspline_data.value("degree", 0), x, sim) * 
           bsplines::dB(j, sim.bspline_data.value("degree", 0), x, sim);
}

PetscErrorCode construct_matrix(const simulation& sim, Mat& M, std::function<std::complex<double>(int, int, std::complex<double>, const simulation&)> integrand,bool use_mpi)
{
    PetscErrorCode ierr;
    int nnz_per_row = 2 * sim.bspline_data.value("degree",0) + 1;

    if (use_mpi)
    {
        ierr = MatCreate(PETSC_COMM_WORLD, &M); CHKERRQ(ierr);
        ierr = MatSetSizes(M, PETSC_DECIDE, PETSC_DECIDE, sim.bspline_data.value("n_basis",0), sim.bspline_data.value("n_basis",0)); CHKERRQ(ierr);
        ierr = MatSetFromOptions(M); CHKERRQ(ierr);
        ierr = MatMPIAIJSetPreallocation(M, nnz_per_row, NULL, nnz_per_row, NULL); CHKERRQ(ierr);
        ierr = MatSetUp(M); CHKERRQ(ierr);
    }
    else
    {
        ierr = MatCreate(PETSC_COMM_SELF, &M); CHKERRQ(ierr);
        ierr = MatSetSizes(M, PETSC_DECIDE, PETSC_DECIDE, sim.bspline_data.value("n_basis",0), sim.bspline_data.value("n_basis",0)); CHKERRQ(ierr);
        ierr = MatSetFromOptions(M); CHKERRQ(ierr);
        ierr = MatSeqAIJSetPreallocation(M, nnz_per_row, NULL); CHKERRQ(ierr);
        ierr = MatSetUp(M); CHKERRQ(ierr);
    }
    
    int start_row,end_row;
    if (use_mpi) {
        ierr = MatGetOwnershipRange(M, &start_row, &end_row); CHKERRQ(ierr);
    } else {
        start_row = 0;
        end_row = sim.bspline_data.value("n_basis", 0);
    }

    for (int i = start_row; i < end_row; i++) 
    {
        int col_start = std::max(0, i - sim.bspline_data.value("order",0) + 1);
        int col_end = std::min(sim.bspline_data.value("n_basis",0), i + sim.bspline_data.value("order",0));

        for (int j = col_start; j < col_end; j++) 
        {
            std::complex<double> result = bsplines::integrate_matrix_element(i, j, integrand, sim);
            ierr = MatSetValue(M, i, j, result.real(), INSERT_VALUES); CHKERRQ(ierr);
        }
    }

    ierr = MatAssemblyBegin(M, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    ierr = MatAssemblyEnd(M, MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    return ierr;
}

PetscErrorCode construct_overlap(const simulation& sim, Mat& S,bool use_mpi)
{
    return construct_matrix(sim, S, bsplines::overlap_integrand, use_mpi);
}

PetscErrorCode construct_kinetic(const simulation& sim, Mat& K,bool use_mpi)
{
    return construct_matrix(sim, K, bsplines::kinetic_integrand, use_mpi);
}

PetscErrorCode construct_invr(const simulation& sim, Mat& Inv_r,bool use_mpi)
{
    return construct_matrix(sim, Inv_r, bsplines::invr_integrand, use_mpi);
}

PetscErrorCode construct_invr2(const simulation& sim, Mat& Inv_r2,bool use_mpi)
{
    return construct_matrix(sim, Inv_r2, bsplines::invr2_integrand, use_mpi);
}

PetscErrorCode construct_der(const simulation& sim, Mat& D,bool use_mpi)
{
    return construct_matrix(sim, D, bsplines::der_integrand, use_mpi);
}

PetscErrorCode save_matrix(Mat A, const char *filename)
    {
        PetscErrorCode ierr;
        PetscViewer viewer;

        // Open a binary viewer in write mode
        ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD, filename, FILE_MODE_WRITE, &viewer); CHKERRQ(ierr);

        // Write the matrix to the file in parallel
        ierr = MatView(A, viewer); CHKERRQ(ierr);

        // Clean up the viewer
        ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);

        return ierr;
    }


// PetscErrorCode SaveMatrixToCSV(Mat M, const std::string& filename) {
//     PetscErrorCode ierr;
//     PetscInt m, n;

//     // Get matrix dimensions
//     ierr = MatGetSize(M, &m, &n); CHKERRQ(ierr);

//     std::ofstream file(filename);
//     if (!file.is_open()) {
//         std::cerr << "Error opening file: " << filename << std::endl;
//         return PETSC_ERR_FILE_OPEN;
//     }

//     // Write matrix values row by row
//     for (PetscInt i = 0; i < m; ++i) {
//         for (PetscInt j = 0; j < n; ++j) {
//             PetscScalar value;
//             ierr = MatGetValues(M, 1, &i, 1, &j, &value); CHKERRQ(ierr);
//             file << value;
//             if (j < n - 1) {
//                 file << ", ";  // Add CSV separator
//             }
//         }
//         file << "\n"; // Newline for next row
//     }

//     file.close();
//     return ierr;
// }

} // namespace bsplines