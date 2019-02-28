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

INITIAL_BALANCES    = [19372, 20625, 20625, 18747, 18372, 16875, 16875, 14997, 14997, 14997]
NOTXS_BALANCES      = [24997, 21250, 20625, 18747, 18372, 16875, 16875, 14997, 14997, 14997]
POWTXS_BALANCES     = [23447, 26874, 21249, 18747, 18372, 16874, 16874, 14997, 14997, 14997]
DPOSTXS_BALANCES    = [19997, 20625, 20625, 18747, 18372, 16875, 16875, 14997, 14997, 14997]
MIXTXS_BALANCES     = [19372, 20625, 20625, 18747, 18372, 16875, 16875, 14997, 14997, 14997]

class dPoS_PositiveTest(dPoS_BaseTest):
    def check_balances(self, balances):
        for n in range(self.num_nodes):
            print(int(self.nodes[n].getbalance() * 100))
#        for n in range(self.num_nodes):
#            assert_equal(int(self.nodes[n].getbalance() * 100), balances[n])

    def check_no_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            self.nodes[n].generate(1)
            time.sleep(1)
            self.sync_all()
            blockCount = blockCount + 1
            for node in self.nodes:
                assert_equal(node.getblockcount(), blockCount)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 321)

    def check_pow_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            reversed_idx = self.num_nodes - n - 1
            self.nodes[n].sendtoaddress(self.operators[reversed_idx], 15.5)
            self.nodes[reversed_idx].generate(1)
            time.sleep(2)
            self.sync_all()
            blockCount = blockCount + 1
            for node in self.nodes:
                assert_equal(node.getblockcount(), blockCount)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 331)

    def check_dpos_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            reversed_idx = self.num_nodes - n - 1
            to_address = self.nodes[reversed_idx].getnewaddress()
            tx = self.create_transaction(n, to_address, 20.2, True)
            time.sleep(2)
            self.sync_all()
            txs = self.nodes[n].list_instant_transactions()
            assert_equal(len(txs), 1)
            assert_equal(txs[0]["hash"], tx)
            self.nodes[reversed_idx].generate(1)
            time.sleep(1)
            self.sync_all()
            blockCount = blockCount + 1
            for node in self.nodes:
                assert_equal(node.getblockcount(), blockCount)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 341)

    def check_mix_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            reversed_idx = self.num_nodes - n - 1
            to_address = self.nodes[reversed_idx].getnewaddress()
            tx = self.create_transaction(n, to_address, 25.25, True)
            self.nodes[n].sendtoaddress(self.operators[reversed_idx], 30.3)
            time.sleep(2)
            self.sync_all()
            txs = self.nodes[n].list_instant_transactions()
            assert_equal(len(txs), 1)
            assert_equal(txs[0]["hash"], tx)
            self.nodes[reversed_idx].generate(1)
            time.sleep(1)
            self.sync_all()
            blockCount = blockCount + 1
            for node in self.nodes:
                assert_equal(node.getblockcount(), blockCount)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 351)

    def check_restart(self):
        vblocks = [node.listdposviceblocks() for node in self.nodes]
        rdvotes = [node.listdposroundvotes() for node in self.nodes]
        txvotes = [node.listdpostxvotes() for node in self.nodes]
        self.stop_nodes()
        self.start_nodes()
        assert_equal(vblocks, [node.listdposviceblocks() for node in self.nodes])
        assert_equal(rdvotes, [node.listdposroundvotes() for node in self.nodes])
        assert_equal(txvotes, [node.listdpostxvotes() for node in self.nodes])

    def run_test(self):
        super(dPoS_PositiveTest, self).run_test()
        print("Creating masternodes")
        mns = self.create_masternodes([0, 3, 4, 7, 8, 9])
        self.check_balances(INITIAL_BALANCES)
        print("Checking block generation with no txs")
        self.check_no_txs()
        self.check_balances(NOTXS_BALANCES)
        print("Checking block generation with PoW txs")
        self.check_pow_txs()
        self.check_balances(POWTXS_BALANCES)
        print("Checking block generation with dPoS txs")
        self.check_dpos_txs()
        self.check_balances(DPOSTXS_BALANCES)
        print("Checking block generation with PoW and dPoS txs")
        self.check_mix_txs()
        self.check_balances(MIXTXS_BALANCES)

        print("Checking restart")
        self.check_restart()

if __name__ == '__main__':
    dPoS_PositiveTest().main()
