Crypticcoin 2.0.0
=============

What is Crypticcoin?
--------------

[Crypticcoin](https://crypticcoin.io/) is an implementation of the "Zerocash" protocol. Crypticcoin also provides 4 types of transactions: instant private transactions, instant transparent transactions, private transactions, transparent transactions.

Based on Bitcoin's and ZCash's code, it intends to offer a far higher standard of privacy, anonimity and security
through a sophisticated zero-knowledge proving scheme that preserves
confidentiality of transaction metadata. Technical details are available
in our [Protocol Specification](https://github.com/zcash/zips/raw/master/protocol/protocol.pdf).

This software is the Crypticcoin client. It downloads and stores the entire history
of Crypticcoin transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

Masternodes
-----------------

To become a masternode, user sends special transaction, which locks 1M CRYP and burns an announcement fee.

Masternodes list can be altered only by special transactions - that's why the list is deterministic. As a result, masternode's reward is fair and doesn't depend on p2p voting.

Announcement fee brings a financial penalty of misbehaving. After dismissing or reassignment, announcement fee isn't refunded (only masternode collateral is refunded). After the MN_Sapling upgrade activation, the amount of announcement fee starts from 1 day worth masternode's income, and slowly grows (during 2 years) until it reaches 31 days worth of income.

Masternode must stay online 24/7. Otherwise it'll get dismissed during dimissal voting.

Instant transactions over Byzantine Fault Tolerance protocol
-----------------

Instant transactions are based on an unique interpretation of Delegated Proof Of Stake consensus, where each block is confirmed by both PoW and dPoS.
Masternodes take part in BFT p2p voting as dPoS validators, ensuring consistency and finality of instant transactions.

Unlike other implementations, CRYP instant transactions may be zero-knowledge private, based on zk-SNARKs. That's why it uses a complex BFT protocol.

The team consists of 32 dPoS validators, and every block one masternode (the oldest one) leaves the team, and a random one (according to PoW hash) joins the team.

Doublesign is mitigated as the p2p protocol allows to find and reject doublesign votes. All the nodes do validate all the p2p votes, and doublesign votes get rejected by the network. Currently, misbehaving masternode won't get dismissed after doublesign attempt, it'll be a part of future updates.

Security Warnings
-----------------

See important security warnings on the
[Security Information page](https://crypticcoin.io/support/security/).

**Crypticcoin is experimental and a work-in-progress.** Use at your own risk.

Deprecation Policy
------------------

This release is considered deprecated 16 weeks after the release day. There
is an automatic deprecation shutdown feature which will halt the node some
time after this 16 week time period. The automatic feature is based on block
height and can be explicitly disabled.


### Need Help?

* Ask for help on the [Crypticcoin](https://forum.crypticcoin.io/) forum.

Participation in the Crypticcoin project is subject to a
[Code of Conduct](code_of_conduct.md).

Building
--------

## Linux
Build Crypticcoin along with most dependencies from source by running
./zcutil/build.sh. Currently only Linux is officially supported.

## Windows
On Ubuntu 18.04 (doesn't work on 16.04):

Get mingw
```
$ sudo apt install mingw-w64
```
Configure to use POSIX variant
```
$ sudo update-alternatives --config x86_64-w64-mingw32-gcc
$ sudo update-alternatives --config x86_64-w64-mingw32-g++
```
Start building
```
$ HOST=x86_64-w64-mingw32 ./zcutil/build.sh
```


## Mac
LIBTOOLIZE=glibtoolize ./zcutil/build.sh

License
-------

For license information see the file [COPYING](COPYING).
