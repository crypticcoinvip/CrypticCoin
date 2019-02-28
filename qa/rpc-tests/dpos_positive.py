#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test dPoS positive scenario
#

import sys
import time
from dpos_base import dPoS_BaseTest
from test_framework.util import \
    assert_equal, \
    assert_greater_than

INITIAL_BALANCES = [19372, 20625, 20625, 18747, 18372, 16875, 16875, 14997, 14997, 14997]
POW_BALANCES = [19372, 20625, 20625, 18747, 18372, 16875, 16875, 14997, 14997, 14997]

class dPoS_PositiveTest(dPoS_BaseTest):
    def check_balances(self, balances):
        for n in range(self.num_nodes):
            assert_equal(int(self.nodes[n].getbalance() * 100), balances[n])

    def check_no_txs(self):
        n = 0
        for node in self.nodes:
            print(n, node.getblockcount())
            node.generate(1)
            time.sleep(3)
            self.sync_all()
            n = n + 1
        for node in self.nodes:
            assert_equal(self.getblockcount(), 321)

    def check_pow_txs(self):
        for n in range(self.num_nodes):
            reversed_idx = self.num_nodes - i - 1
            node.sendtoaddress(self.operators[reversed_idx], 5)
            self.nodes[reversed_idx].generate(1)
            self.sync_all()
        for node in self.nodes:
            assert_equal(self.getblockcount(), 331)
        for n in range(self.num_nodes):
            print(int(self.nodes[n].getbalance() * 100))

    def check_dpos_txs(self):
        pass

    def check_mix_txs(self):
        pass

    def run_test(self):
        super(dPoS_PositiveTest, self).run_test()
        print("Creating masternodes")
        mns = self.create_masternodes([0, 3, 4, 7, 8, 9])
        self.check_balances(INITIAL_BALANCES)
        print("Checking block generation with no txs")
        self.check_no_txs()
        print("Checking block generation with PoW txs")
        self.check_pow_txs()
        print("Checking block generation with dPoS txs")
        self.check_dpos_txs()
        print("Checking block generation with PoW and dPoS txs")
        self.check_mix_txs()

        for node in self.nodes:
            print(node.listdposviceblocks())
            print(node.listdposroundvotes())
            print(node.listdpostxvotes())
        self.stop_nodes()

if __name__ == '__main__':
    dPoS_PositiveTest().main()
