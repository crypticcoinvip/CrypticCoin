#ifndef SOLVER_H
#define SOLVER_H

#include "equi_miner.h"

#include <functional>
#include <vector>

bool TrompSolve(//unsigned int n, unsigned int k,
                 const crypto_generichash_blake2b_state & curr_state,
                 const std::function<bool(std::vector<unsigned char>)> validBlock);

#endif // SOLVER_H
