#ifndef MNTYPES_H
#define MNTYPES_H

#include <map>
#include <set>

class uint256;
class CMasternode;
class CDismissVote;
class CKeyID;
struct COperatorUndoRec;

typedef std::map<uint256, CMasternode> CMasternodes;  // nodeId -> masternode object,
typedef std::set<uint256> CActiveMasternodes;         // just nodeId's,
typedef std::map<CKeyID, uint256> CMasternodesByAuth; // for two indexes, owner->nodeId, operator->nodeId

typedef std::map<uint256, std::pair<int32_t, CKeyID> > CTeam;   // nodeId -> <joinHeight, operatorId> - masternodes' team

typedef std::map<uint256, CDismissVote> CDismissVotes;
typedef std::multimap<uint256, uint256> CDismissVotesIndex; // just index, from->against or against->from


#endif // MNTYPES_H
