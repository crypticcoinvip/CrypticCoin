FROM ubuntu:14.04

RUN apt-get update

RUN apt-get install -y build-essential
RUN apt-get install -y git
RUN apt-get install -y software-properties-common python-software-properties

RUN add-apt-repository ppa:bitcoin/bitcoin
RUN apt-get update

RUN apt-get install -y libdb4.8-dev libdb4.8++-dev libtool autotools-dev automake pkg-config libssl-dev libevent-dev \
    bsdmainutils git libboost-all-dev libminiupnpc-dev libqt5gui5 libqt5core5a libqt5webkit5-dev libqt5dbus5 qttools5-dev qttools5-dev-tools \
    libprotobuf-dev protobuf-compiler libqrencode-dev

#CrypticCoin
RUN mkdir /root/CrypticCoin
RUN git clone https://github.com/crypticcoin/crypticcoin.git /root/CrypticCoin
RUN cd /root/CrypticCoin/ ; /root/CrypticCoin/autogen.sh
RUN cd /root/CrypticCoin/ ; /root/CrypticCoin/configure
RUN cd /root/CrypticCoin/ ; make

RUN chmod 0777 /root/CrypticCoin/src/*

ADD ./conf/CrypticCoin.conf /root/.CrypticCoin/CrypticCoin.conf

WORKDIR /root/CrypticCoin

EXPOSE 23303 23202

#CMD printenv | grep -v "yyyyyxxxx" >> /etc/environment && tail -f /dev/null

CMD /root/CrypticCoin/src/CrypticCoind -printtoconsole
