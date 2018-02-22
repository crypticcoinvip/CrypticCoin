//
// Created by Konstantin on 21/02/2018.
//

#include "main.h";
/*
 * Check if block achieved a year mark
 * accept height of block
 * */
bool IsYearBlockHeight(int nHeight) {
    for (unsigned short int i = 0; i < ARRAYLEN(YEAR_BLOCKS); i++) {
        if (YEAR_BLOCKS[i] == nHeight)
            return true;
    }
    return false;
}

bool AddInflationOutputInTx(CTransaction &tx, CPubKey pubkey) {
    tx.vout.resize(2);
    tx.vout[1].scriptPubKey << pubkey << OP_CHECKSIG;
    tx.vout[1].nValue = INFLATION;
    return true;
}
