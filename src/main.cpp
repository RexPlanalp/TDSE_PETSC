#include <slepcsys.h>
#include <slepceps.h>
#include <petscmat.h>
#include <petscsys.h>

#include "simulation.h"
#include "bsplines.h"
#include "tise.h"


#include <string>
#include <iostream>

int main(int argc, char **argv) {
    PetscErrorCode ierr;
    ierr = SlepcInitialize(&argc, &argv, NULL, NULL); CHKERRQ(ierr);
    int rank,size;
    MPI_Comm_rank(PETSC_COMM_WORLD,&rank);
    MPI_Comm_size(PETSC_COMM_WORLD,&size);

    std::string input_file = "input.json";

    simulation sim(input_file);
    sim.save_debug_info(rank);

    
    bsplines::save_debug_bsplines(rank,sim);

    Mat S;
    //tise tise_solver;
    double start = MPI_Wtime();
    bsplines::construct_overlap(sim,S);
    double end = MPI_Wtime();
    PetscPrintf(PETSC_COMM_WORLD,"Time to construct overlap matrix %.3f\n",end-start);
    

    






   
    
   
   
    ierr = SlepcFinalize(); CHKERRQ(ierr);
    return 0;
}


