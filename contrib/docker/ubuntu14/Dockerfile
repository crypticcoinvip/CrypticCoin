# Dockerfile for CrypticCoin
# http://CrypticCoin.io/
# https://github.com/NOTEXIST/crypticcoin/crypticcoin.git

FROM ubuntu:14.04

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update --yes > /dev/null && \
    apt-get upgrade --yes > /dev/null && \
    apt-get install --yes -qq software-properties-common > /dev/null && \
    add-apt-repository --yes ppa:bitcoin/bitcoin && \
    apt-get update --yes > /dev/null && \
    apt-get upgrade --yes > /dev/null && \
    apt-get install --yes -qq \
       autoconf \
       automake \
       autotools-dev \
       bsdmainutils \
       build-essential \
       git \
       libboost-all-dev \
       libboost-filesystem-dev \
       libboost-program-options-dev \
       libboost-system-dev \
       libboost-test-dev \
       libboost-thread-dev \
       libdb4.8++-dev \
       libdb4.8-dev \
       libevent-dev \
       libminiupnpc-dev \
       libprotobuf-dev \
       libqrencode-dev \
       libqt5core5a \
       libqt5dbus5 \
       libqt5gui5 \
       libqt5webkit5-dev  \
       libsqlite3-dev \
       libssl-dev \
       libtool \
       pkg-config \
       protobuf-compiler \
       qt5-default \
       qtbase5-dev \
       qtdeclarative5-dev \
       qttools5-dev \
       qttools5-dev-tools \
      > /dev/null

RUN git clone https://github.com/NOTEXIST/crypticcoin/crypticcoin.git /coin/git

WORKDIR /coin/git

RUN ./autogen.sh && ./configure --with-gui=qt5 && make && mv src/CrypticCoind /coin/CrypticCoind

WORKDIR /coin
VOLUME ["/coin/home"]

ENV HOME /coin/home

CMD ["/coin/CrypticCoind"]

# P2P, RPC
EXPOSE 23303 23202
