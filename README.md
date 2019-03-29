Crypticcoin 2.0.1
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

Instant transactions over BFT protocol
-----------------

Instant transactions are based on an unique interpretation of Delegated Proof Of Stake (dPoS) consensus, where each block is confirmed by both PoW and dPoS.
Masternodes take part in Byzantine Fault Tolerant (BFT) p2p voting as validators, ensuring consistency and finality of instant transactions.

Unlike other implementations, CRYP instant transactions may be zero-knowledge private, based on zk-SNARKs. That's why it uses a complex BFT protocol instead of TxLocks.

The team consists of 32 dPoS validators, and every block one masternode (the oldest one) leaves the team, and a random one (according to PoW hash) joins the team.

Doublesign is mitigated as the p2p protocol allows to find and reject doublesign votes. All the nodes do validate all the p2p votes, and doublesign votes get rejected by the network. Currently, misbehaving masternode won't get dismissed after doublesign attempt, it'll be a part of future updates.

Masternodes short manual
-----------------
The full docker manual is avalible at https://github.com/crypticcoinvip/docker

#### For owners who operate their masternodes
1. Use an Ubuntu 16.04/18.04 server with at least 300GB of disk space available, 2 CPU cores, 4GB RAM.
2. Install docker on your machine. On Ubuntu 16.04/18.04:
```
cat /etc/apt/sources.list # check that universe repo is enabled
sudo apt-get update
sudo apt-get install docker.io
```
3. Announce masternode with your owner reward address (the script will take care of other addresses). THE OPERATION WILL BURN ANNOUNCEMENT FEE! Don't do it you're not sure that the address is correct.
```
wget -qO- https://raw.githubusercontent.com/crypticcoinvip/docker/master/tools | ownerRewardAddress=YOUR-REWARD-ADDRESS bash /dev/stdin mn_announce
```
4. Ensure that the server is online 24/7 - you'll get dismissed instead.
5. If needed, resign and refund your collateral (retrieve your MASTERNODE_ID by calling ```./tools cli mn_list [] true```):
```
./tools cli mn_resign MASTERNODE_ID new_t-address
```

#### For owners who outsource operation
Your operator should provide you with an instruction. The process will have the following steps:
1. Announce masternode (or call mn_setoperator on an announced masternode), specify operator's reward ratio, operator's addresses and YOUR collateral address, YOUR owner auth address, YOUR owner reward address.
2. Most importantly, to prevent a fraud, collateral must be stored on an address which is controlled by owner, not operator.
2. Operator runs your masternode. Both owner and operator do receive their reward shares.
3. If operator misbehaves, it may lead to masternode's dismissal.
4. Check that operator sends heartbeats by calling:
```crypticcoin-cli mn_filterheartbeats recently```
Check that status of your masternode is ```"activated"```.

#### For masternode operators
The general flow is as below:
1. Operator generates 2 addresses: operator auth address, operator reward address
2. Owner and operator agree on a certain operator reward ratio.
3. Operator provides owner with his addresses.
4. Owner announces masternode or calls mn_setoperator on an announced masternode.
5. Operator ensures that owner specified correct addresses and a certain operator reward ratio. Owner may change these parameters at any time, so it should be re-checked every block.
6. Operator restarts the node with ```-masternode_operator=YOUR-OPERATOR-AUTH-ADDRESS```.
7. Both owner and operator do receive their reward shares.

#### For developers
If you know what you're doing, and you need to look under the hood, then just check ```tools``` script at https://github.com/crypticcoinvip/docker .

The process of masternode announcement looks like this:

- Owner calls ```crypticcoin-cli mn_announce [] '{"name":"nodename","ownerAuthAddress":"address1","operatorAuthAddress":"address2","ownerRewardAddress":"address3","operatorRewardAddress":"address4","operatorRewardRatio":0,"collateralAddress":"address5"}'```
- Sending this transaction doesn't mean that you're starting to operate masternode. To start operating masternode, add ```masternode_operator=YOUR-OPERATOR-AUTH-ADDRESS```, ```txindex=1``` into the config and restart the operator's node. You'll need to do ```-reindex``` if ```-txindex``` wasn't set before.
- Make sure that you have at least 10 CRYP on operator's wallet on any transparent address (aside collateral).
- You probably want to specify ```masternode_owner``` in owner's config to call owner's RPC commands.
- No need to call ```mn_activate``` manually, the operator will self-activate after a certain height.
- Node uses only Tor connections by default.

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
