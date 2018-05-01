#!/bin/bash
#// full deployement : run sh go.sh

# generating entropy make it harder to guess the randomness!.
echo "Initializing random number generator..."
random_seed=/var/run/random-seed
# Carry a random seed from start-up to start-up
# Load and then save the whole entropy pool
if [ -f $random_seed ]; then
    sudo cat $random_seed >/dev/urandom
else
    sudo touch $random_seed
fi
sudo chmod 600 $random_seed
poolfile=/proc/sys/kernel/random/poolsize
[ -r $poolfile ] && bytes=`sudo cat $poolfile` || bytes=512
sudo dd if=/dev/urandom of=$random_seed count=1 bs=$bytes

#Also, add the following lines in an appropriate script which is run during the$

# Carry a random seed from shut-down to start-up
# Save the whole entropy pool
echo "Saving random seed..."
random_seed=/var/run/random-seed
sudo touch $random_seed
sudo chmod 600 $random_seed
poolfile=/proc/sys/kernel/random/poolsize
[ -r $poolfile ] && bytes=`sudo cat $poolfile` || bytes=512
sudo dd if=/dev/urandom of=$random_seed count=1 bs=$bytes

# Create a swap file

cd ~
if [ -e /swapfile1 ]; then
echo "Swapfile already present"
else
sudo dd if=/dev/zero of=/swapfile1 bs=1024 count=524288
sudo mkswap /swapfile1
sudo chown root:root /swapfile1
sudo chmod 0600 /swapfile1
sudo swapon /swapfile1
fi

# Install dependency

sudo apt-get -y install software-properties-common

sudo add-apt-repository -y ppa:bitcoin/bitcoin

sudo apt-get update

sudo apt-get -y install libcanberra-gtk-module

# Dont need to check if bd is already installed, will override or pass by
#results=$(find /usr/ -name libdb_cxx.so)
#if [ -z $results ]; then
sudo apt-get -y install libdb4.8-dev libdb4.8++-dev
#else
#grep DB_VERSION_STRING $(find /usr/ -name db.h)
#echo "BerkeleyDb will not be installed its already there...."
#fi

sudo apt-get -y install git build-essential libtool autotools-dev autoconf automake pkg-config libssl-dev libevent-dev bsdmainutils git libprotobuf-dev protobuf-compiler libqrencode-dev

sudo apt-get -y install libqt5gui5 libqt5core5a libqt5webkit5-dev libqt5dbus5 qttools5-dev qttools5-dev-tools

sudo apt-get -y install libminiupnpc-dev

sudo apt-get -y install libseccomp-dev

sudo apt-get -y install libcap-dev

# Keep current version of libboost if already present
results=$(find /usr/ -name libboost_chrono.so)
if [ -z $results ]; then
sudo apt-get -y install libboost-all-dev
else
red=`tput setaf 1`
green=`tput setaf 2`
reset=`tput sgr0`
echo "${red}Libboost will not be installed its already there....${reset}"
grep --include=*.hpp -r '/usr/' -e "define BOOST_LIB_VERSION"
fi

sudo apt-get -y install --no-install-recommends gnome-panel

sudo apt-get -y install lynx

sudo apt-get -y install unzip

cd ~

#// Compile Berkeley if 4.8 is not there
if [ -e /usr/lib/libdb_cxx-4.8.so ]
then
echo "BerkeleyDb already present...$(grep --include *.h -r '/usr/' -e 'DB_VERSION_STRING')" 
else
wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz 
tar -xzvf db-4.8.30.NC.tar.gz 
rm db-4.8.30.NC.tar.gz
cd db-4.8.30.NC/build_unix 
../dist/configure --enable-cxx 
make 
sudo make install 
sudo ln -s /usr/local/BerkeleyDB.4.8/lib/libdb-4.8.so /usr/lib/libdb-4.8.so
sudo ln -s /usr/local/BerkeleyDB.4.8/lib/libdb_cxx-4.8.so /usr/lib/libdb_cxx-4.8.so
cd ~
sudo rm -Rf db-4.8.30.NC
#sudo ldconfig
fi

#// Check if libboost is present

results=$(find /usr/ -name libboost_chrono.so)
if [ -z $results ]; then
sudo rm download
     wget https://sourceforge.net/projects/boost/files/boost/1.63.0/boost_1_63_0.zip/download 
     unzip -o download
     cd boost_1_63_0
	sh bootstrap.sh
	sudo ./b2 install
	cd ~
	sudo rm download 
	sudo rm -Rf boost_1_63_0
	#sudo ln -s $(dirname "$(find /usr/ -name libboost_chrono.so)")/lib*.so /usr/lib
	sudo ldconfig
        #sudo rm /usr/lib/libboost_chrono.so
else
     echo "Libboost found..." 
     grep --include=*.hpp -r '/usr/' -e "define BOOST_LIB_VERSION"
fi

#// Clone files from repo, Permissions and make

#git clone --recurse-submodules http://git.sfxdx.ru/cryptic/CrypticCoin
git clone -b new-genesis-block --single-branch --recurse-submodules http://git.sfxdx.ru/cryptic/CrypticCoin
cd CrypticCoin
sudo bash autogen.sh
chmod 777 ~/CrypticCoin/share/genbuild.sh
chmod 777 ~/CrypticCoin/src/leveldb/build_detect_platform



grep --include=*.hpp -r '/usr/' -e "define BOOST_LIB_VERSION"

sudo rm wrd01.txt
sudo rm wrd00.txt
sudo rm words
find /usr/ -name libboost_chrono.so > words
split -dl 1 --additional-suffix=.txt words wrd



if [ -e wrd01.txt ]; then
echo 0. $(cat wrd00.txt)
echo 1. $(cat wrd01.txt)
echo 2. $(cat wrd02.txt)
echo 3. $(cat wrd03.txt)
echo -n "Choose libboost library to use(0-3)?"
read answer
else
echo "There is only 1 libboost library present. We choose for you 0"
answer=0
fi

echo "You have choosen $answer"

if [ -d /usr/local/BerkeleyDB.4.8/include ]
then
sudo ./configure CPPFLAGS="-I/usr/local/BerkeleyDB.4.8/include -O2" LDFLAGS="-L/usr/local/BerkeleyDB.4.8/lib" --with-gui=qt5 --with-boost-libdir=$(dirname "$(cat wrd0$answer.txt)")
echo "Using Berkeley Generic..."
else
sudo ./configure --with-gui=qt5 --with-boost-libdir=$(dirname "$(cat wrd0$answer.txt)")
echo "Using default system Berkeley..."
fi

sudo make -j$(nproc)

if [ -e ~/CrypticCoin/src/qt/CrypticCoin-qt ]; then
#sudo apt-get -y install pulseaudio
#sudo apt-get -y install portaudio19-dev
# synthetic voice 
#cd ~
#wget https://sourceforge.net/projects/espeak/files/espeak/espeak-1.48/espeak-1.48.04-source.zip/download
#unzip -o download
#cd espeak-1.48.04-source/src
#cp portaudio19.h portaudio.h
#make
#cd ~
sudo strip ~/CrypticCoin/src/CrypticCoind
sudo strip ~/CrypticCoin/src/qt/CrypticCoin-qt
sudo make install
else
echo "Compile fail not CrypticCoin-qt present"
fi

cd ~

#// Create the config file with random user and password

mkdir -p ~/.CrypticCoin
if [ -e ~/.CrypticCoin/CrypticCoin.conf ]; then
    cp -a ~/.CrypticCoin/CrypticCoin.conf ~/.CrypticCoin/CrypticCoin.bak
fi
echo -e "rpcuser="$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 26 ; echo '')"\n""rpcpassword="$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 26 ; echo '')"\n""rpcport=23202""\n""port=23303""\n""daemon=1""\n""listen=1""\n""server=1""\n""addnode=jhbkhdxegbeb5zbn.onion"> ~/.CrypticCoin/CrypticCoin.conf

# Create Icon on Desktop and in menu
mkdir -p ~/Desktop/
sudo cp ~/CrypticCoin/src/qt/res/icons/CrypticCoin.png /usr/share/icons/
echo -e '#!/usr/bin/env xdg-open'"\n""[Desktop Entry]""\n""Version=1.0""\n""Type=Application""\n""Terminal=false""\n""Icon[en]=/usr/share/icons/CrypticCoin.png""\n""Name[en]=CrypticCoin""\n""Exec=CrypticCoin-qt""\n""Name=CrypticCoin""\n""Icon=/usr/share/icons/CrypticCoin.png""\n""Categories=Network;Internet;" > ~/Desktop/CrypticCoin.desktop
sudo chmod +x ~/Desktop/CrypticCoin.desktop
sudo cp ~/Desktop/CrypticCoin.desktop /usr/share/applications/CrypticCoin.desktop
sudo chmod +x /usr/share/applications/CrypticCoin.desktop

# Erase all CrypticCoin compilation directory , cleaning

cd ~
#sudo rm -Rf ~/crypticcoin

#// Start CrypticCoin

CrypticCoin-qt
if [ -e ~/.CrypticCoin/wallet.dat ]; then
    cp ~/.CrypticCoin/wallet.dat ~/CrypticCoinwallet.bak
fi