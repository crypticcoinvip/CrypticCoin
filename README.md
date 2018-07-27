Crypticcoin 1.1.0
=============

What is Crypticcoin?
--------------

[Crypticcoin](https://z.cash/) is an implementation of the "Zerocash" protocol.
Based on Bitcoin's code, it intends to offer a far higher standard of privacy
through a sophisticated zero-knowledge proving scheme that preserves
confidentiality of transaction metadata. Technical details are available
in our [Protocol Specification](https://github.com/crypticcoin/zips/raw/master/protocol/protocol.pdf).

This software is the Crypticcoin client. It downloads and stores the entire history
of Crypticcoin transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

Security Warnings
-----------------

See important security warnings on the
[Security Information page](https://z.cash/support/security/).

**Crypticcoin is experimental and a work-in-progress.** Use at your own risk.

Deprecation Policy
------------------

This release is considered deprecated 16 weeks after the release day. There
is an automatic deprecation shutdown feature which will halt the node some
time after this 16 week time period. The automatic feature is based on block
height and can be explicitly disabled.

Where do I begin?
-----------------
We have a guide for joining the main Crypticcoin network:
https://github.com/crypticcoin/crypticcoin/wiki/1.0-User-Guide

### Need Help?

* See the documentation at the [Crypticcoin Wiki](https://github.com/crypticcoin/crypticcoin/wiki)
  for help and more information.
* Ask for help on the [Crypticcoin](https://forum.crypticcoin.io/) forum.

Participation in the Crypticcoin project is subject to a
[Code of Conduct](code_of_conduct.md).

Building
--------

## Linux
Build Crypticcoin along with most dependencies from source by running
./zcutil/build.sh. Currently only Linux is officially supported.

## Windows
$ sudo apt install mingw-w64
$ sudo update-alternatives --config x86_64-w64-mingw32-gcc
(configure to use POSIX variant)
$ sudo update-alternatives --config x86_64-w64-mingw32-g++
(configure to use POSIX variant)
$ HOST=x86_64-w64-mingw32 ./zcutil/build.sh

## Mac
LIBTOOLIZE=glibtoolize ./zcutil/build.sh

License
-------

For license information see the file [COPYING](COPYING).
