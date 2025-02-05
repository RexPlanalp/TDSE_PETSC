#include <iostream>
#include <fstream>
#include <petscmat.h>
#include <petscvec.h>
#include <petscviewerhdf5.h>
#include "simulation.h"
#include "bsplines.h"
#include <map>
#include <iomanip>
#include <algorithm>
#include <complex>
#include "misc.h"
#include <complex>
#include <map>




namespace pes
{

    PetscErrorCode load_final_state(const char* filename, Vec* state, int total_size) 
    {   
        PetscErrorCode ierr;
        PetscViewer viewer;

        ierr = VecCreate(PETSC_COMM_SELF, state); CHKERRQ(ierr);
        ierr = VecSetSizes(*state, PETSC_DECIDE, total_size); CHKERRQ(ierr);
        ierr = VecSetFromOptions(*state); CHKERRQ(ierr);
        ierr = VecSetType(*state, VECMPI); CHKERRQ(ierr);
        ierr = VecSet(*state, 0.0); CHKERRQ(ierr);

        // TESTING
        ierr = PetscObjectSetName((PetscObject)*state, "final_state"); CHKERRQ(ierr);
        //ierr = PetscObjectSetName((PetscObject)*state, "psi_final"); CHKERRQ(ierr);
        // TESTING
        ierr = PetscViewerHDF5Open(PETSC_COMM_SELF, filename, FILE_MODE_READ, &viewer); CHKERRQ(ierr);
        ierr = VecLoad(*state, viewer); CHKERRQ(ierr);
        ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);


        return ierr;
    }

    PetscErrorCode project_out_bound(const char* filename, Mat& S, Vec& state, int n_basis, int n_blocks, int nmax, std::map<int, std::pair<int, int>>& block_to_lm)
    {
        PetscErrorCode ierr;
        Vec state_block, tise_state,temp;
        IS is;
        std::complex<double> inner_product;
        PetscBool has_dataset;
        PetscViewer viewer;

        // Open HDF5 file for reading
        ierr = PetscViewerHDF5Open(PETSC_COMM_SELF, filename, FILE_MODE_READ, &viewer); CHKERRQ(ierr);

        const char GROUP_PATH[] = "/eigenvectors";  // Path to the datasets

        for (int idx = 0; idx < n_blocks; ++idx)
        {
            std::pair<int, int> lm_pair = block_to_lm.at(idx);
            int l = lm_pair.first;
            int m = lm_pair.second;

            int start = idx * n_basis;
            ierr = ISCreateStride(PETSC_COMM_SELF, n_basis, start, 1, &is); CHKERRQ(ierr);
            ierr = VecGetSubVector(state, is, &state_block); CHKERRQ(ierr);
            ierr = VecDuplicate(state_block, &temp); CHKERRQ(ierr);

          

            for (int n = 0; n <= nmax; ++n)
            {
                std::ostringstream dataset_name;
                // TESTING
                 dataset_name << GROUP_PATH << "/psi_" << n << "_" << l;
                //dataset_name << "/Psi_" << n << "_" << l;
                // TESTING
                ierr = PetscViewerHDF5HasDataset(viewer, dataset_name.str().c_str(), &has_dataset); CHKERRQ(ierr);
                if (has_dataset)
                {   
                    ierr = VecDuplicate(state_block, &tise_state); CHKERRQ(ierr);
                    ierr = VecSet(tise_state, 0.0); CHKERRQ(ierr);

                    ierr = PetscObjectSetName((PetscObject)tise_state, dataset_name.str().c_str()); CHKERRQ(ierr);
                    ierr = VecLoad(tise_state, viewer); CHKERRQ(ierr);

                    ierr = MatMult(S,state_block,temp); CHKERRQ(ierr);
                    ierr = VecDot(temp,tise_state,&inner_product); CHKERRQ(ierr);
                    ierr = VecAXPY(state_block,-inner_product,tise_state); CHKERRQ(ierr); // Subtract projection 
                }
            }
            
            

            ierr = VecRestoreSubVector(state, is, &state_block); CHKERRQ(ierr);
            ierr = ISDestroy(&is); CHKERRQ(ierr);
            ierr = VecDestroy(&temp); CHKERRQ(ierr);
        }

        ierr = PetscViewerDestroy(&viewer); CHKERRQ(ierr);
        ierr = VecDestroy(&tise_state); CHKERRQ(ierr);

        return ierr;
    }

    double H(double r)
    {
        return -1.0/(r+1E-25);
    }

    void scale_vector(std::vector<double>& vec, double scale)
    {
        for (double& val : vec)
        {
            val *= scale;
        }
    }

    struct CoulombResult 
    {
        double phase;
        std::vector<double> wave;
    };

    CoulombResult compute_coulomb_wave(double E, int l, int Nr, double dr) {
    std::vector<double> wave(Nr, 0.0);
    const double dr2 = dr * dr;
    const double k = std::sqrt(2.0 * E);
    const int lterm = l * (l + 1);

    wave[0] = 0.0;
    wave[1] = 1.0;

    for (int idx = 2; idx < Nr; ++idx) {
        const double r_val = idx * dr;
        const double term = dr2 * (lterm/(r_val*r_val) + 2.0*H(r_val) - 2.0*E);
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

    return CoulombResult{phase, wave};
}
    
    PetscErrorCode expand_state(Vec& state,std::vector<std::complex<double>>& expanded_state,int Nr, int n_blocks,int n_basis, int degree, double dr, const std::vector<std::complex<double>>& knots, std::map<int, std::pair<int, int>>& block_to_lm)
    {   
        // Load the state into easily accessible array (avoid VecGetValue calls)
        PetscErrorCode ierr;
        const std::complex<double>* state_array;
        ierr =  VecGetArrayRead(state, reinterpret_cast<const PetscScalar**>(&state_array)); CHKERRQ(ierr);

        // Loop over all bspline basis function
        for (int bspline_idx = 0; bspline_idx < n_basis; ++bspline_idx)
        {   
            // Get the start and end of the bspline basis function
            std::complex<double> start = knots[bspline_idx];
            std::complex<double> end = knots[bspline_idx+degree+1];

            // Initialize vectors to store the evaluation of the bspline basis function and the corresponding indices
            std::vector<std::complex<double>> bspline_eval;
            std::vector<int> bspline_eval_indices;

            // Loop over all grid points
            for (int r_idx = 0; r_idx < Nr; ++r_idx)
            {   
                std::complex<double> r = r_idx*dr;
                if (r.real() >= start.real() && r.real() < end.real())
                {
                    std::complex<double> val = bsplines::B(bspline_idx,degree,r,knots);
                    bspline_eval.push_back(val);
                    bspline_eval_indices.push_back(r_idx);
                }
            }

            // Loop over each block 
            for (int block = 0; block < n_blocks; ++block)
            {   
                std::complex<double> coeff = state_array[block*n_basis + bspline_idx];
                // Loop over all grid points and add contribution to the expanded state for this block
                for (int r_sub_idx = 0; r_sub_idx < bspline_eval.size(); ++r_sub_idx)
                {
                    expanded_state[block*Nr + bspline_eval_indices[r_sub_idx]] += coeff*bspline_eval[r_sub_idx];
                }
            }
        }
    }

    std::map<std::pair<double,int>,std::pair<std::vector<double>,double>> compute_coulomb_map(double Emax, double dE, int lmax, int Nr, double dr)
    {   
        int Ne = static_cast<int>(Emax/dE) + 1;
        std::cout << "Ne" << " " << Ne << std::endl;
        std::map<std::pair<double,int>,std::pair<std::vector<double>,double>> coulomb_map;
        for (int l = 0; l <= lmax; ++l)
        {
            for (int E_idx = 1;  E_idx < Ne; ++E_idx)
            {
                double E = E_idx*dE;
                CoulombResult result = compute_coulomb_wave(E, l, Nr, dr);
                coulomb_map[std::make_pair(E,l)] = std::make_pair(result.wave,result.phase);
            }

        }

        return coulomb_map;
    }

    std::map<std::pair<int,int>,std::vector<std::complex<double>>> compute_partial_spectra(const std::vector<std::complex<double>>& expanded_state, std::map<std::pair<double,int>,std::pair<std::vector<double>,double>>& coulomb_map, double Emax, double dE,int n_blocks,std::map<int, std::pair<int, int>>& block_to_lm, int Nr,double dr)
    {
        std::map<std::pair<int,int>,std::vector<std::complex<double>>> partial_spectra;
        int Ne = static_cast<int>(Emax/dE) + 1;
        std:: cout << "Ne" << " " << Ne << std::endl;
        for (int block = 0; block < n_blocks; ++block)
        {
            
            std::pair<int,int> lm_pair = block_to_lm.at(block);
            int l = lm_pair.first;
            int m = lm_pair.second;
            partial_spectra[std::make_pair(l, m)].reserve(Ne); 
        }

        for (int E_idx = 1; E_idx < Ne; ++E_idx)
        {
            for (int block = 0; block < n_blocks; ++block)
            {
                
                std::pair<int,int> lm_pair = block_to_lm.at(block);
                int l = lm_pair.first;
                int m = lm_pair.second;

                std::vector<double> coulomb_wave = coulomb_map.at(std::make_pair(E_idx*dE,l)).first;
                auto start = expanded_state.begin() + Nr*block;  // Starting at index 2
                auto end = expanded_state.begin() + Nr*(block+1);    // Ending before index 5

                std::vector<std::complex<double>> block_vector(start, end);  // Subvector containing elements 3, 4, 5

                std::vector<std::complex<double>> result;
                pes_pointwise_mult(coulomb_wave,block_vector,result);
                std::complex<double> I = pes_simpsons_method(result,dr);   
                partial_spectra[std::make_pair(l,m)].push_back(I);

            }
        }


        
       

        return partial_spectra;
    }

    void compute_photoelectron(const std::map<std::pair<int,int>,std::vector<std::complex<double>>>& partial_spectra,int n_blocks,double Emax, double dE,std::map<int, std::pair<int, int>>& block_to_lm)
    {   

        std::ofstream pesFiles("pes.txt", std::ios::app);
        int Ne = static_cast<int>(Emax/dE) + 1;

        std::vector<std::complex<double>> pes(Ne,0.0);
       
        for (int block = 0; block < n_blocks; ++block)
        {   
            std::pair<int,int> lm_pair = block_to_lm.at(block);
            int l = lm_pair.first;
            int m = lm_pair.second;

            std::vector<std::complex<double>> magsq(Ne,0.0);
            
            pes_pointwise_magsq(partial_spectra.at(std::make_pair(l,m)),magsq);
            pes_pointwise_add(pes,magsq,pes);
        }
        

        for (int idx = 0; idx < pes.size(); ++idx)
        {   
            std::cout << idx * dE << std::endl;
            pesFiles << idx*dE << " " << pes[idx].real() << " " << pes[idx].imag() << "\n";
        }

        pesFiles.close();

    }

   
    int compute_pes(int rank,const simulation& sim)
    {   
        if (rank!=0)
        {
            return 0;
        }

        int Nr = sim.grid_data.at("Nr").get<int>();
        double dr = sim.grid_data.at("grid_spacing").get<double>();
        int n_blocks = sim.angular_data.at("n_blocks").get<int>();
        int n_basis = sim.bspline_data.at("n_basis").get<int>();
        int degree = sim.bspline_data.at("degree").get<int>();
        int nmax = sim.angular_data.at("nmax").get<int>();
        double Emax = sim.observable_data.at("E").get<std::array<double,2>>()[1];
        double dE = sim.observable_data.at("E").get<std::array<double,2>>()[0];
        int lmax = sim.angular_data.at("lmax").get<int>();
        std::map<int, std::pair<int, int>> block_to_lm = sim.block_to_lm;

        Vec final_state;
        load_final_state("TDSE_files/tdse_output.h5", &final_state, n_blocks*n_basis);

        Mat S;
        PetscErrorCode ierr;
        ierr = bsplines::construct_overlap(sim,S,false,false); CHKERRQ(ierr);

        ierr = project_out_bound("TISE_files/tise_output.h5", S, final_state, n_basis, n_blocks, nmax, block_to_lm); CHKERRQ(ierr);

        std::vector<std::complex<double>> expanded_state (Nr * n_blocks,0.0);
        expand_state(final_state,expanded_state,Nr,n_blocks,n_basis,degree,dr,sim.knots,block_to_lm);

        std::map<std::pair<double,int>,std::pair<std::vector<double>,double>> coulomb_map = compute_coulomb_map(Emax,dE,lmax,Nr,dr);


        std::map<std::pair<int,int>,std::vector<std::complex<double>>> partial_spectra = compute_partial_spectra(expanded_state,coulomb_map,Emax,dE,n_blocks,block_to_lm,Nr,dr);


        compute_photoelectron(partial_spectra,n_blocks,Emax,dE,block_to_lm);

        // for (const auto& entry : partial_spectra) {
        //     const auto& key = entry.first;     // This is the pair<int,int>
        //     const auto& spectrum = entry.second;  // This is the vector<complex<double>>
            
        //     std::string filename = "partial_" + std::to_string(key.first) + "_" + std::to_string(key.second) + ".txt";
            
        //     std::ofstream outfile(filename);
        //     if (!outfile.is_open()) {
        //         std::cerr << "Failed to open " << filename << std::endl;
        //         continue;  // Skip to next spectrum if file can't be opened
        //     }

        //     for (int idx = 0; idx < spectrum.size(); ++idx) {
        //         outfile << idx*dE << " " << spectrum[idx].real() << " " << spectrum[idx].imag() << "\n";
        //     }
        //     outfile.flush();
        //     outfile.close();
            
        //     std::cout << "Wrote " << filename << std::endl;
        // }

        // int block_idx = 1;
        // std::ofstream outFile("expanded_state.txt");
        // for (int idx = 0; idx < Nr; ++idx)
        // {
        //     outFile << idx*dr << " " << expanded_state[block_idx*Nr + idx].real() << " " << expanded_state[block_idx*Nr + idx].imag() << "\n";
        // }
        // outFile.close();

        return 0;
    }


}