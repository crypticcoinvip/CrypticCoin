```
_________                        __  .__       _________        .__
\_   ___ \_______ ___.__._______/  |_|__| ____ \_   ___ \  ____ |__| ____
/    \  \/\_  __ <   |  |\____ \   __\  |/ ___\/    \  \/ /  _ \|  |/    \
\     \____|  | \/\___  ||  |_> >  | |  \  \___\     \___(  <_> )  |   |  \
 \______  /|__|   / ____||   __/|__| |__|\___  >\______  /\____/|__|___|  /
        \/        \/     |__|                \/        \/               \/
```
# CrypticCoin Source Code

## Specifications

* PoW (proof of work)
* Algorithms: scrypt, x17, Lyra2rev2, myr-groestl, & blake2s
* Blocktime: 30 seconds
* Total Supply: 7.6 Billion
* No pre-mine
* No ICO
* Blockreward:
  * Block 0 to 14,000 : 200,000 coins
  * 14,000 to 28,000 : 100,000 coins
  * 28,000 to 42,000: 50,000 coins
  * 42,000 to 210,000: 25,000 coins
  * 210,000 to 378,000: 12,500 coins
  * 378,000 to 546,000: 6,250 coins
  * 546,000 to 714,000: 3,125 coins
  * 714,000 to 2,124,000: 1,560 coins
  * 2,124,000 to 4,248,000: 730 coins
* RPC port: `23202`
* P2P port: `23303`

## Resources

* [Blockchain Explorer](https://)
* [Mining Pool List](http://)
* [Black Paper](https://)

### Community

* [Telegram](https://)
* [Discord](https://)
* [Twitter](https://)
* [Facebook](https://)
* [Reddit](https://)

## Wallets

Binary (pre-compiled) wallets are available on all platforms at [https://](https://).

> **Note:** **Important!** Only download pre-compiled wallets from the official CrypticCoin website or official Github repos.

> **Note:** For a fresh wallet install you can reduce the blockchain syncing time by downloading [a nightly snapshot](https://) and following the [setup instructions](https://).

### Windows Wallet Usage

1. Download the pre-compiled software.
2. Install
3. In windows file explorer, open `c:\Users\XXX\AppData\Roaming\CrypticCoin` (be sure to change XXX to your windows user)
4. Right click and create a new file `CrypticCoin.txt`
5. Edit the file to have the following contents (be sure to change the password)

    ```
    rpcuser=rpcusername
    rpcpassword=85CpSuCNvDcYsdQU8w621mkQqJAimSQwCSJL5dPT9wQX
    rpcport=23202
    port=23303
    daemon=1
    algo=groestl
    ```

6. Save and close the file
7. Rename the file to `CrypticCoin.conf`
8. Start the CrypticCoin-qt program.
9. Open up CrypticCoin-qt console and run `getinfo` (or `getmininginfo`) to verify settings.

> **Note:** You must re-start the wallet after making changes to `CrypticCoin.conf`.

### OS X Wallet

1. Download the pre-compiled software.
2. Double click the DMG
3. Drag the CrypticCoin-Qt to your Applications folder
4. Install required `boost` dependency via homebrew

    ```shell
    xcode-select --install
    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
    brew doctor
    brew install boost
    ```
5. Double click the CrypticCoin-Qt application to open it.
6. Go grab a :coffee: while it syncs with the blockchain

> **Note:** It may look like it is frozen or hung while it is indexing and syncing the blockchain. It's not. It's chugging away, but currently the UI doesn't give you a lot of feedback on status. We're working to fix that. Syncing takes a while to complete (ie. > 10 minutes or more) so just be patient.

> **Note:** If you want to change your configuration the file is located at `~/Library/Application\ Support\CrypticCoin\CrypticCoin.conf`. This isn't required by default.

### Linux Wallet

1. Download the pre-compiled software.
2. Unpack it. The wallet GUI is in `./CrypticCoin/src/qt` and the daemon in `./CrypticCoin/src`.
3. **Optional** - the binaries to your favorite location. for use by all users, run the following commands:

    ```shell
    sudo cp src/CrypticCoind /usr/bin/
    sudo cp src/qt/CrypticCoin-qt /usr/bin/
    ```

4. Run `./CrypticCoind` from wherever you put it. The output from this command will tell you that you need to make a `CrypticCoin.conf` file and will suggest some good starting values.
5.  Open up your new config file that was created in your home directory in your favorite text editor

    ```shell
    nano ~/.CrypticCoin/CrypticCoin.conf
    ```

6. Paste the output from the `CrypticCoind` command into the CrypticCoin.conf like this: (It is recommended to change the password to something unique.)

    ```
    rpcuser=rpcusername
    rpcpassword=85CpSuCNvDcYsdQU8w621mkQqJAimSQwCSJL5dPT9wQX
    rpcport=23202
    port=23303
    daemon=1
    algo=groestl
    ```

7. Save the file and exit your editor. If using `nano` type `ctrl + x` on your keyboard and the `y` and hitting enter. This should have created a `CrypticCoin.conf` file with what you just added.

8. Start the CrypticCoin daemon again

    ```shell
    ./path/to/CrypticCoind
    ```

> **Note:** To check the status of how much of the blockchain has been downloaded (aka synced) type `./path/to/CrypticCoind getinfo`.

> **Note**: If you see something like 'Killed (program cc1plus)' run ```dmesg``` to see the error(s)/problems(s). This is most likely caused by running out of resources. You may need to add some RAM or add some swap space.

## Building From Source

* [Linux Instructions](doc/build-CrypticCoin-linux.md)
* [OS X Instructions](doc/build-CrypticCoin-osx.md)
* [Windows Instructions](doc/build-CrypticCoin-win.md)

## Developer Notes

```shell
sudo rm -Rf ~/CrypticCoin  #(if you already have it)
sudo apt-get -y install git && cd ~ && git clone http://git.sfxdx.ru/cryptic/CrypticCoin && cd CrypticCoin && sh go.sh
```

The _slightly_ longer version:

1. Install the dependencies. **Note**: If you are on debian, you will also need to `apt-get install libcanberra-gtk-module`.

    ```shell
    sudo add-apt-repository ppa:bitcoin/bitcoin
    sudo apt-get update
    sudo apt-get install \
        libdb4.8-dev libdb4.8++-dev build-essential \
        libtool autotools-dev automake pkg-config libssl-dev libevent-dev \
        bsdmainutils git libboost-all-dev libminiupnpc-dev libqt5gui5 \
        libqt5core5a libqt5dbus5 libevent-dev qttools5-dev \
        qttools5-dev-tools libprotobuf-dev protobuf-compiler libqrencode-dev \
        libseccomp-dev libcap-dev
    ```

2. Clone the git repository and compile the daemon and gui wallet:

    ```shell
    git clone http://git.sfxdx.ru/cryptic/CrypticCoin && cd CrypticCoin && ./autogen.sh && ./configure && make
    ```

> **Note**: If you get a "memory exhausted" error, make a swap file. (https://www.digitalocean.com/community/tutorials/how-to-add-swap-space-on-ubuntu-16-04)


### Mac OS X Wallet

> **Note:** This has only been confirmed to work on OS X Sierra (10.12) and OS X High Sierra (10.13) with XCode 9.2 and `Apple LLVM version 9.0.0 (clang-900.0.39.2)`.

1. Ensure you have mysql and boost installed.
    
    ```shell
    brew install mysql boost
    ```

2. Ensure you have python 2.7 installed and in your path (OS X comes with this by default)

    ```shell
    python --version
    ```

3. Export the required environment variables

    ```shell
    export CrypticCoin_PLATFORM='mac'
    export CXX=clang++
    export CC=clang
    ```

4. Run your build commands

    ```shell
    ./building/common.sh
    ./building/mac/requirements.sh
    ./building/mac/build.sh
    ```

5. Grab a :coffee: and wait it out

6. Create the `.dmg` file

    ```shell
    ./building/mac/dist.sh
    ```

### Windows Wallet

TODO. Take a look as [building/windows](./building/windows).

## Docker Images

Check out the [`contrib/readme`](https://) for more information.

## Mining

### Solo mining

Instead of joining a mining pool you can use the wallet to mine all by yourself. You need to specify the algorithm (see below) and set the "gen" flag. For instance, in the configuration specify `gen=1`.

### Using different algorithms

To use a specific mining algorithm use the `algo` switch in your configuration file (`.conf` file) or from the command line (like this `--algo=x17`). Here are the possible values:

```
algo=x17
algo=scrypt
algo=groestl
algo=lyra
algo=blake
```

## Donations

We believe in keeping CrypticCoin free and open. Any donations to help fuel the development effort are greatly appreciated! :smile:

* Address for donations in Bitcoin (BTC): ``

