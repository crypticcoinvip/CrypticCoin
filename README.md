Crypticcoin 1.1.2
=============

What is Crypticcoin?
--------------

[Crypticcoin](https://crypticcoin.io/) is an implementation of the "Zerocash" protocol.
Based on Bitcoin's code, it intends to offer a far higher standard of privacy
through a sophisticated zero-knowledge proving scheme that preserves
confidentiality of transaction metadata. Technical details are available
in our [Protocol Specification](https://github.com/zcash/zips/raw/master/protocol/protocol.pdf).

This software is the Crypticcoin client. It downloads and stores the entire history
of Crypticcoin transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

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
See mingw64 branch

## Mac
LIBTOOLIZE=glibtoolize ./zcutil/build.sh

License
-------

For license information see the file [COPYING](COPYING).
