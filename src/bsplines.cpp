#include "bsplines.h"
#include "simulation.h"
#include <cmath>
#include "gauss.h"
#include <fstream>
#include <iostream>
#include <functional>



std::complex<double> bsplines::B(int i, int degree, std::complex<double> x,const simulation& sim)
{
    if (degree == 0)
    {
        return (sim.complex_knots[i].real() <= x.real() && x.real() < sim.complex_knots[i+1].real() ? 1.0 : 0.0);
    }


    std::complex<double> denom1 = sim.complex_knots[i + degree] - sim.complex_knots[i];
    std::complex<double> denom2 = sim.complex_knots[i + degree + 1] - sim.complex_knots[i + 1];

    std::complex<double> term1 = 0.0;
    std::complex<double> term2 = 0.0;

    if (denom1.real() > 0)
    {
        term1 = (x-sim.complex_knots[i]) / (denom1) * B(i,degree-1,x,sim);
    }
    if (denom2.real()>0)
    {
        term2 = (sim.complex_knots[i+degree+1] -x ) / (denom2) * B(i+1,degree-1,x,sim);
    }

    return term1+term2;


}

std::complex<double> bsplines::dB(int i, int degree, std::complex<double> x,const simulation& sim)
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
        term1 = std::complex<double>(degree)/(denom1) * B(i,degree-1,x,sim);
    }
    if (denom2.real()>0)
    {
        term2 = -std::complex<double>(degree)/(denom2) * B(i+1,degree-1,x,sim);
    }

    return term1+term2;
}

void bsplines::save_debug_bsplines(int rank, const simulation& sim)
{
    if (!sim.bspline_data.value("debug",0)) return; // Only save if debugging is enabled

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

        for (int i = 0; i <sim.bspline_data.value("n_basis",0); i++)
        {
            for (int idx = 0; idx < sim.grid_data.value("Nr",0); ++idx)
            {
                double x_val = rmin + idx * dr;
                std::complex<double> x = sim.ecs_x(x_val);
                std::complex<double> val = B(i,sim.bspline_data.value("degree",0),x,sim);
                file1 << val.real() << "\t" << val.imag() << "\n";
            }
            file1 << "\n";
        }
        file1.close();

        std::ofstream file2("dbsplines.txt");
        if (!file2.is_open())
        {
            std::cerr << "Error: could not open file bsplines.txt" << std::endl;
            return;
        }

        for (int i = 0; i <sim.bspline_data.value("n_basis",0); i++)
        {
            for (int idx = 0; idx < sim.grid_data.value("Nr",0); ++idx)
            {
                double x_val = rmin + idx * dr;
                std::complex<double> x = sim.ecs_x(x_val);
                std::complex<double> val = dB(i,sim.bspline_data.value("degree",0),x,sim);
                file2 << val.real() << "\t" << val.imag() << "\n";
            }
            file2 << "\n";
        }
        file2.close();
    }
}
    
std::complex<double> bsplines::integrate_matrix_element(int i, int j, std::function<std::complex<double>(int,int,std::complex<double>)> integrand,const simulation& sim)
{

    std::complex<double> total = 0.0;
    int lower = std::min(i, j);
    int upper = std::max(i, j);

    std::vector<double> roots = gauss::get_roots(sim.bspline_data.value("order",0));
    std::vector<double> weights = gauss::get_weights(sim.bspline_data.value("order",0));

    for (int k = lower; k<=upper+sim.bspline_data.value("degree",0); ++k)
    {
        double a = sim.knots[k];
        double b = sim.knots[k+1];

        if (a==b)
        {
            continue;
        }

        double half_b_minus_a = 0.5*(b-a);
        double half_b_plus_a = 0.5*(b+a);

        for (int r = 0; r<roots.size(); ++r)
        {
            double x_val = half_b_minus_a * roots[r] + half_b_plus_a;
            double weight_val = weights[r];

            std::complex<double> x = sim.ecs_x(x_val);
            std::complex<double> weight = sim.ecs_w(x_val, weight_val)*half_b_minus_a;
            total += weight*integrand(i,j,x);

            
        }


    }
        
    return total;
}

std::complex<double> bsplines::overlap_integrand(int i, int j, std::complex<double> x,const simulation& sim)
{
    return bsplines::B(i,sim.bspline_data.value("degree",0),x,sim)*bsplines::B(j,sim.bspline_data.value("degree",0),x,sim);
}

