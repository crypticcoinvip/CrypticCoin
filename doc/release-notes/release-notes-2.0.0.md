Notable changes
===============

Masternodes
-----------------

To become a masternode, user sends the special transaction, which locks 1M CRYP and burns an announcement fee.

Masternodes list can be altered only by special transactions - that's why the list is deterministic. As a result, masternode's reward is fair and doesn't depend on p2p voting.

Announcement fee brings a financial penalty of misbehaving. After dismissing or reassignment, announcement fee isn't refunded (only masternode collateral is refunded). After the MN_Sapling upgrade activation, the amount of announcement fee starts from 1 day worth masternode's income, and slowly grows (during 2 years) until it reaches 31 days worth of income.

Masternode must stay online 24/7. Otherwise it'll get dismissed during dimissal voting.

Instant transactions over BFT protocol
-----------------

Instant transactions are based on an unique interpretation of Delegated Proof Of Stake (dPoS) consensus, where each block is confirmed by both PoW and dPoS.
Masternodes take part in Byzantine Fault Tolerant (BFT) p2p voting as validators, ensuring consistency and finality of instant transactions.

Unlike other implementations, CRYP instant transactions may be zero-knowledge private, based on zk-SNARKs. That's why it uses a complex BFT protocol instead of TxLocks.

The team consists of 32 dPoS validators, and every block one masternode (the oldest one) leaves the team, and a random one (according to PoW hash) joins the team.

Doublesign is mitigated as the p2p protocol allows to find and reject doublesign votes. All the nodes do validate all the p2p votes, and doublesign votes get rejected by the network. Currently, misbehaving masternode won't get dismissed after doublesign attempt, it'll be a part of future updates.


Sapling network upgrade
-----------------
The activation height for the Sapling network upgrade on mainnet is included
in this release. Sapling will activate on mainnet at height 124475, which is
expected to be mined on the 26th of March 2019. Please upgrade to this release,
or any subsequent release, in order to follow the Sapling network upgrade.

Changelog
=========

IntegralTeam (14):
      integration with Zcash 2.0.3 Sapling network upgrade
      mn: masternodes DB
      mn: AnnounceMasternode tx
      mn: ActivateMasternode tx
      mn: CollateralSpent tx
      mn: SetOperator tx
      mn: DismissVote tx
      mn: DismissVoteRecall tx
      mn: FinalizeDismissVoting tx
      mn: prune masternodes DB
      mn: masternode automation: auth outputs generation, self-activation, dismissal voting against MN heartbeats
      dpos: dPoS team calculation
      dpos: dPoS team reward
      mn: RPC tests

IntegralTeam (8):
      mn: heartbeats
      dpos: dPoS p2p messages (vice-blocks, vice-block votes, tx votes)
      dpos: dPoS DB (vice-blocks, vice-block votes, tx votes)
      dpos: prune dPoS DB
      dpos: instant transaction flag
      dpos: dPoS controller (maintenance events loop, voter wrappers)
      dpos: update submitblock/getblocktemplate RPC calls to support vice-blocks generation by pool
      dpos: RPC tests

IntegralTeam (6):
      dpos: BFT voting engine
      dpos: block validation
      dpos: protection against DDoS by flooding instant txs. Voter exhaustion rules
      dpos: p2p syncing instant transactions via both mempool and dPoS controller
      dpos: disable dPoS if passed 24 hours since last block
      dpos: voter unit tests

