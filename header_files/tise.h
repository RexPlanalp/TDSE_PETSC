#pragma once

#include "data.h"
#include <vector>
#include <string>
#include <petscmat.h>
#include "matrix.h"

struct tise : public data
{


tise(std::string& filename);
matrix compute_overlap_matrix(std::string matrix_type,bsplines basis);



};