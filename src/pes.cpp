#include <iostream>
#include <fstream>
#include <map>
#include <algorithm>
#include <complex>


#include <sys/stat.h>
#include <sys/types.h>

#include <petscmat.h>
#include <petscvec.h>
#include <petscviewerhdf5.h>

#include "simulation.h"
#include "bsplines.h"
#include "petsc_wrappers/PetscMatrix.h"
#include "petsc_wrappers/PetscVector.h"
#include "petsc_wrappers/PetscFileViewer.h"
#include "utility.h"
#include "utility.h"

using lm_pair = std::pair<int, int>;
using energy_l_pair = std::pair<double, int>;

namespace pes
{
    struct coulomb_wave 
    {
        double phase;
        std::vector<double> wave;
    };

    coulomb_wave compute_coulomb_wave(double E, int l, int Nr, double dr, std::function<std::complex<double>(double)> Pot){
    std::vector<double> wave(Nr, 0.0);
    const double dr2 = dr * dr;
    const double k = std::sqrt(2.0 * E);
    const int lterm = l * (l + 1);

    wave[0] = 0.0;
    wave[1] = 1.0;

    for (int idx = 2; idx < Nr; ++idx) {
        const double r_val = idx * dr;
        const double term = dr2 * (lterm/(r_val*r_val) + 2.0*Pot(r_val).real() - 2.0*E);
        wave[idx] = wave[idx - 1] * (term + 2.0) - wave[idx - 2];

        // Match Python's overflow handling
        if (std::abs(wave[idx]) > 1E10) {
            double max_val = *std::max_element(wave.begin(), wave.end(), 
                [](double a, double b) { return std::abs(a) < std::abs(b); });
            scale_vector(wave, 1.0/max_val);
        }
    }

    const double r_end = (Nr - 2) * dr;
    const double wave_end = wave[Nr - 2];
    const double dwave_end = (wave[Nr - 1] - wave[Nr - 3]) / (2.0 * dr);
    
    // Match Python's normalization approach
    const double denom = k + 1.0/(k * r_end);
    const double termPsi = std::abs(wave_end) * std::abs(wave_end);
    const double termDer = std::abs(dwave_end/denom) * std::abs(dwave_end/denom);
    const double normVal = std::sqrt(termPsi + termDer);

    if (normVal > 0.0) {
        scale_vector(wave, 1.0/normVal);
    }

    std::complex<double> numerator(0.0, wave_end);
    numerator += dwave_end / denom;

    const double scale = 2.0 * k * r_end;
    const std::complex<double> denomC = std::exp(std::complex<double>(0.0, 1.0/k) * std::log(scale));
    const std::complex<double> fraction = numerator / denomC;
    const double phase = std::arg(fraction) - k * r_end + l * M_PI/2.0;

    return coulomb_wave{phase, wave};
}
    
    std::vector<std::complex<double>> expand_state(const PetscVector& state, const simulation& sim)
    {   
        PetscErrorCode ierr;
        const std::complex<double>* state_array;
        ierr =  VecGetArrayRead(state.vector, reinterpret_cast<const PetscScalar**>(&state_array)); checkErr(ierr, "Error in VecGetArrayRead");
        
        std::vector<std::complex<double>> expanded_state(sim.grid_params.Nr * sim.angular_params.n_blocks,0.0);

        // Loop over all bspline basis function
        for (int bspline_idx = 0; bspline_idx < sim.bspline_params.n_basis; ++bspline_idx)
        {   
            // Get the start and end of the bspline basis function
            std::complex<double> start = sim.bspline_params.knots[bspline_idx];
            std::complex<double> end = sim.bspline_params.knots[bspline_idx+sim.bspline_params.degree+1];

            // Initialize vectors to store the evaluation of the bspline basis function and the corresponding indices
            std::vector<std::complex<double>> bspline_eval;
            std::vector<int> bspline_eval_indices;

            // Loop over all grid points
            for (int r_idx = 0; r_idx < sim.grid_params.Nr; ++r_idx)
            {   
                std::complex<double> r = r_idx*sim.grid_params.dr;
                if (r.real() >= start.real() && r.real() < end.real())
                {
                    std::complex<double> val = bsplines::B(bspline_idx,sim.bspline_params.degree,r,sim.bspline_params.knots);
                    bspline_eval.push_back(val);
                    bspline_eval_indices.push_back(r_idx);
                }
            }

            // Loop over each block 
            for (int block = 0; block < sim.angular_params.n_blocks; ++block)
            {   
                std::complex<double> coeff = state_array[block*sim.bspline_params.n_basis + bspline_idx];
                // Loop over all grid points and add contribution to the expanded state for this block
                for (size_t r_sub_idx = 0; r_sub_idx < bspline_eval.size(); ++r_sub_idx)
                {
                    expanded_state[block*sim.grid_params.Nr + bspline_eval_indices[r_sub_idx]] += coeff*bspline_eval[r_sub_idx];
                }
            }
        }
        return  expanded_state;
    }

    void compute_partial_spectra(const std::vector<std::complex<double>>& expanded_state,std::map<lm_pair,std::vector<std::complex<double>>>& partial_spectra,std::map<energy_l_pair,double> phases, const simulation& sim)
    {
        for (int block = 0; block < sim.angular_params.n_blocks; ++block)
        {
            
            lm_pair lm_pair = sim.angular_params.block_to_lm.at(block);
            int l = lm_pair.first;
            int m = lm_pair.second;
            partial_spectra[std::make_pair(l, m)].reserve(sim.observable_params.Ne); 
        }

        std::function<std::complex<double>(std::complex<double>)> Pot;
        switch(sim.potential_type)
        {
            case PotentialType::H:
                Pot = H;
                break;
            case PotentialType::He:
                Pot = He;
                break;
        }

        for (int E_idx = 1; E_idx <= sim.observable_params.Ne; ++E_idx)
        {

            if (sim.debug)
            {
                std::cout << "Computing Partial Spectrum for E = " << (E_idx*sim.observable_params.dE) << "\n\n";
            }

            for (int block = 0; block < sim.angular_params.n_blocks; ++block)
            {
                
                lm_pair lm_pair = sim.angular_params.block_to_lm.at(block);
                int l = lm_pair.first;
                int m = lm_pair.second;
                coulomb_wave coulomb_result = compute_coulomb_wave(E_idx*sim.observable_params.dE, l, sim.grid_params.Nr, sim.grid_params.dr,Pot);
                
                phases[std::make_pair(E_idx*sim.observable_params.dE,l)] = coulomb_result.phase;


                auto start = expanded_state.begin() + sim.grid_params.Nr*block;  
                auto end = expanded_state.begin() + sim.grid_params.Nr*(block+1);    

                std::vector<std::complex<double>> block_vector(start, end);  

                std::vector<std::complex<double>> result;
                pes_pointwise_mult(coulomb_result.wave,block_vector,result);
                std::complex<double> I = pes_simpsons_method(result,sim.grid_params.dr);   
                partial_spectra[std::make_pair(l,m)].push_back(I);

            }
        }
        return;
    }

    void compute_angle_integrated(const std::map<lm_pair,std::vector<std::complex<double>>>& partial_spectra, const simulation& sim)
    {   

        std::ofstream pesFiles("PES_files/pes.txt", std::ios::app);
        std::vector<std::complex<double>> pes(sim.observable_params.Ne,0.0);
       
        for (int block = 0; block < sim.angular_params.n_blocks; ++block)
        {   
            lm_pair lm_pair = sim.angular_params.block_to_lm.at(block);
            int l = lm_pair.first;
            int m = lm_pair.second;

            std::vector<std::complex<double>> magsq(sim.observable_params.Ne,0.0);
            
            pes_pointwise_magsq(partial_spectra.at(std::make_pair(l,m)),magsq);
            pes_pointwise_add(pes,magsq,pes);
        }
        

        for (size_t idx = 1; idx <pes.size(); ++idx)
        {   
            if (sim.debug)
            {
                std::cout << "Computing Angle Integrated Spectrum for E = " << sim.observable_params.dE*idx << "\n\n";
            }
            std::complex<double> val = pes[idx];
            val /= ((2*M_PI)*(2*M_PI)*(2*M_PI));
            pesFiles << idx*sim.observable_params.dE << " " << val.real() << " " << "\n";
        }

        pesFiles.close();
    }

    void compute_angle_resolved(const std::map<lm_pair,std::vector<std::complex<double>>>& partial_spectra,std::map<energy_l_pair,double> phases,const simulation& sim)
    {
        std::ofstream padFiles("PES_files/pad.txt", std::ios::app);
        std::vector<double> theta_range;
        std::vector<double> phi_range;

        if (sim.observable_params.SLICE == "XZ")
        {
            for (double theta = 0; theta <= M_PI; theta += 0.01) 
            {
                theta_range.push_back(theta);
            }

            phi_range  = {0.0,M_PI};
        }
        if (sim.observable_params.SLICE  == "XY")
        {
            theta_range = {M_PI/ 2.0};

            for (double phi = 0; phi < 2.0*M_PI; phi += 0.01) 
            {
                phi_range.push_back(phi);
            }

        }
        if (sim.observable_params.SLICE  != "XZ" && sim.observable_params.SLICE  != "XY") {
            throw std::invalid_argument("Invalid SLICE value: " + sim.observable_params.SLICE );
        }

        for (int E_idx = 1; E_idx <= sim.observable_params.Ne; ++E_idx)
        {   

            double E = E_idx*sim.observable_params.dE;
            double k = std::sqrt(2.0*E);

            if (sim.debug)
            {
                std::cout << "Computing Angle Resolved Spectrum for E = " << E << "\n\n";
            }

            for (auto& theta : theta_range)
            {
                for (auto& phi : phi_range)
                {   
                    std::complex<double> pad_amplitude {};
                    for (const auto& pair: partial_spectra)
                    {
                        int l = pair.first.first;
                        int m = pair.first.second;

                        std::complex<double> sph_term = compute_Ylm(l,m,theta,phi);

                        double partial_phase = phases[std::make_pair(E,l)];
                        std::complex<double> partial_amplitude = pair.second[E_idx - 1];

                        std::complex<double> phase_factor = std::exp(std::complex<double>(0.0,3*l*M_PI/2.0 + partial_phase));

                        pad_amplitude += sph_term*phase_factor*partial_amplitude;

                    }

                    double pad_prob = std::norm(pad_amplitude);
                    pad_prob/=((2*M_PI)*(2*M_PI)*(2*M_PI));
                    pad_prob/=k;

                    padFiles << E << " " << theta << " " << phi << " " << pad_prob << "\n";
                }
            }
        }
    }

    int compute_pes(int rank,const simulation& sim)
    {   
        if (rank!=0)
        {
            return 0;
        }
        
        create_directory(rank,sim.pes_output_path);

        std::cout << "Constructing Overlap Matrix" << std::endl;
        RadialMatrix S(sim, RunMode::SEQUENTIAL, ECSMode::OFF);
        S.populateMatrix(sim,bsplines::overlap_integrand);
        

        std::cout << "Loading Final State" << "\n\n";
        PetscHDF5Viewer finalStateViewer((sim.tdse_output_path+"/tdse_output.h5").c_str(),RunMode::SEQUENTIAL,OpenMode::READ);
        PetscVector final_state = finalStateViewer.loadVector(sim.angular_params.n_blocks*sim.bspline_params.n_basis,"","final_state");
        

        std::cout << "Projecting out bound states" << "\n\n";
        project_out_bound(S,final_state,sim);

        
        std::cout << "Expanding State in Position Space" << "\n\n";
        std::vector<std::complex<double>> expanded_state = expand_state(final_state,sim);

        std::cout << "Computing Partial Spectra" << "\n\n";
        std::map<energy_l_pair,double> phases;
        std::map<lm_pair,std::vector<std::complex<double>>> partial_spectra;
        
        compute_partial_spectra(expanded_state,partial_spectra,phases,sim);

        std::cout << "Computing Angle Integrated Spectrum" << "\n\n";
        compute_angle_integrated(partial_spectra,sim);

        std::cout << "Computing Angle Resolved Spectrum" << "\n\n";
        compute_angle_resolved(partial_spectra,phases,sim);
        return 0;
    }
}