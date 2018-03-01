//
// Created by Konstantin on 21/02/2018.
//

#include "main.h";
/*
 * Check if block achieved a year mark
 * accept height of block
 * */
bool IsBlockForInflation(int nHeight) { return nHeight % NUMBER_OF_BLOCKS_PER_WEEK == 0; }

bool AddInflationOutputInTx(CTransaction &tx, CPubKey pubkey) {
    if (!pubkey.IsValid()) {
        printf("PubKey for Inflation Wallet is not valid\n");
    } else {
        printf("PubKey is fine!\n");
    }
    tx.vout.resize(2);
    tx.vout[1].scriptPubKey << pubkey << OP_CHECKSIG;
    tx.vout[1].nValue = INFLATION;
    return true;
}
