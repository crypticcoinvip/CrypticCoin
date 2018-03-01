//
// Created by Konstantin on 21/02/2018.
//

#ifndef CRYPTICCOIN_INFLATION_H
#define CRYPTICCOIN_INFLATION_H

class CTransaction;
class CPubKey;

bool IsBlockForInflation(int nHeight);
bool AddInflationOutputInTx(CTransaction &tx, CPubKey pubkey);

#endif //CRYPTICCOIN_INFLATION_H
