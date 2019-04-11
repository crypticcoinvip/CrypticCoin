#ifndef MNTYPES_H
#define MNTYPES_H

#include <map>
#include <set>
#include "pubkey.h"

class uint256;
class CMasternode;
class CDismissVote;
class CKeyID;
struct COperatorUndoRec;

typedef std::map<uint256, CMasternode> CMasternodes;  // nodeId -> masternode object,
typedef std::set<uint256> CActiveMasternodes;         // just nodeId's,
typedef std::map<CKeyID, uint256> CMasternodesByAuth; // for two indexes, owner->nodeId, operator->nodeId

struct TeamData
{
    int32_t joinHeight;
    CKeyID  operatorAuth;
};

typedef std::map<uint256, TeamData> CTeam;   // nodeId -> <joinHeight, operatorAuth> - masternodes' team

typedef std::map<uint256, CDismissVote> CDismissVotes;
typedef std::multimap<uint256, uint256> CDismissVotesIndex; // just index, from->against or against->from


#endif // MNTYPES_H
