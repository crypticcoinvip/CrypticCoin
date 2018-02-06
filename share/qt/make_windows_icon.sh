#!/bin/bash
# create multiresolution windows icon
ICON_DST=../../src/qt/res/icons/CrypticCoin.ico

convert ../../src/qt/res/icons/CrypticCoin-16.png ../../src/qt/res/icons/CrypticCoin-32.png ../../src/qt/res/icons/CrypticCoin-48.png ${ICON_DST}
