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


INITIAL_BALANCES    = [10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000]
MNS_BALANCES        = [18122, 20000, 11000, 8122,  8122,  10000, 10000, 8122,  8122,  8122 ]
NOTXS_BALANCES      = [18122, 20000, 20000, 9122,  8122,  10000, 10000, 8122,  8122,  8122 ]
POWTXS_BALANCES     = [4708,  6110,  6110,  12233, 15858, 6735,  6110,  6233,  6233,  6233 ]
DPOSTXS_BALANCES    = [24997, 26874, 26874, 19372, 18372, 16874, 16874, 14997, 14997, 14997]
MIXTXS_BALANCES     = [24997, 26874, 26874, 24997, 18997, 16874, 16874, 14997, 14997, 14997]

class dPoS_PositiveTest(dPoS_BaseTest):
    def check_balances(self, balances, strict=False):
        for n in range(self.num_nodes):
            print(int(self.nodes[n].getbalance() * 100))
        if strict:
            [assert_equal(int(self.nodes[n].getbalance() * 100), balances[n]) for n in range(self.num_nodes)]
        else:
            for n in range(self.num_nodes):
                bl = float(balances[n]) / 1000
                br = float(self.nodes[n].getbalance()) / 10
                assert_greater_than(0.1, abs(br - bl) / bl) # br - bl < 10%

    def check_no_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount() + 1
            self.nodes[n].generate(1)
            self.wait_block(self.nodes[n], blockCount)

    def check_pow_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount() + 1
            next_idx = n + 1 if n + 1 < self.num_nodes else 0
            zdst = [{ "address": self.nodes[next_idx].getnewaddress(), "amount": 11.11 }]
            ztx = self.nodes[n].z_sendmany(self.operators[n], zdst, 1, 0.0001, False)
            time.sleep(2)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            print(ztx)
            assert_equal(ztx[0]["status"], "success")
            self.nodes[n].sendtoaddress(self.nodes[next_idx].getnewaddress(), 25.25)
            self.nodes[self.num_nodes - n - 1].generate(1)
            self.wait_block(self.nodes[n], blockCount)

    def check_dpos_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            next_idx = n + 1 if n + 1 < self.num_nodes else 0
            toaddr = self.nodes[next_idx].getnewaddress()
            tx = self.create_transaction(n, toaddr, 20.2, True)
            zdst = [{ "address": toaddr, "amount": 3.3 }]
            ztx = self.nodes[n].z_sendmany(self.operators[n], zdst, 1, 0.0001, True)
            time.sleep(4)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            print(ztx)
            assert_equal(ztx[0]["status"], "success")
            self.sync_all()
            for node in self.nodes:
                txs = {tx["hash"] for tx in node.i_listtransactions()}
                assert_equal(len(txs), 2)
                assert_equal(txs, {tx, ztx[0]["result"]["txid"]})
            self.nodes[self.num_nodes - n - 1].generate(1)
            time.sleep(2)
            self.sync_all()
            blockCount = blockCount + 1
            self.check_nodes_block_count(blockCount)

    def check_mix_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            next_idx = n + 1 if n + 1 < self.num_nodes else 0
            toaddr1 = self.nodes[next_idx].getnewaddress()
            toaddr2 = self.nodes[next_idx].getnewaddress()
            zdst = [{ "address": toaddr1, "amount": 1.2 }, { "address": toaddr2, "amount": 2.1 }]
            tx1 = self.create_transaction(n, toaddr1, 25.25, True)
            self.nodes[n].sendtoaddress(self.operators[next_idx], 30.3)
            tx2 = self.create_transaction(n, toaddr2, 25.25, True)
            self.nodes[n].sendtoaddress(self.operators[next_idx], 30.3)
            ztx = self.nodes[n].z_sendmany(self.operators[n], zdst, 1, 0.0001, True)
            time.sleep(5)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            assert_equal(ztx[0]["status"], "success")
            self.sync_all()
            for node in self.nodes:
                txs = {tx["hash"] for tx in node.i_listtransactions()}
                assert_equal(len(txs), 3)
                assert_equal(txs, {tx1, tx2, ztx[0]["result"]["txid"]})
            self.nodes[self.num_nodes - n - 1].generate(1)
            time.sleep(2)
            self.sync_all()
            blockCount = blockCount + 1
            self.check_nodes_block_count(blockCount)

    def check_restart(self):
        vblocks_before = [node.dpos_listviceblocks() for node in self.nodes]
        rdvotes_before = [node.dpos_listroundvotes() for node in self.nodes]
        txvotes_before = [node.dpos_listtxvotes() for node in self.nodes]
        self.stop_nodes()
        self.start_masternodes()
        self.connect_nodes()
        vblocks_after = [node.dpos_listviceblocks() for node in self.nodes]
        rdvotes_after = [node.dpos_listroundvotes() for node in self.nodes]
        txvotes_after = [node.dpos_listtxvotes() for node in self.nodes]
        assert_equal(vblocks_before, vblocks_after)
        assert_equal(len(rdvotes_before), len(rdvotes_after))
        for i in range(len(rdvotes_before)):
            print i
            assert_equal(len(rdvotes_before[i]), len(rdvotes_after[i]))
            for j in range(len(rdvotes_before[i])):
                print j
                assert_equal(rdvotes_before[i][j], rdvotes_after[i][j])
        assert_equal(rdvotes_before, rdvotes_after)
        assert_equal(txvotes_before, txvotes_after)

    def check_reindex(self):
        [assert_greater_than(len(node.dpos_listviceblocks()), 0) for node in self.nodes]
        [assert_greater_than(len(node.dpos_listroundvotes()), 0) for node in self.nodes]
        [assert_greater_than(len(node.dpos_listtxvotes()), 0) for node in self.nodes]
        self.stop_nodes()
        self.start_masternodes([['-reindex']] * self.num_nodes)
        self.connect_nodes()
        time.sleep(5)
        [assert_equal(len(node.dpos_listviceblocks()), 0) for node in self.nodes]
        [assert_equal(len(node.dpos_listroundvotes()), 0) for node in self.nodes]
        [assert_equal(len(node.dpos_listtxvotes()), 0) for node in self.nodes]

    def run_test(self):
        super(dPoS_PositiveTest, self).run_test()
        self.check_balances(INITIAL_BALANCES, True)
        print("Creating masternodes")
        mns_idxs = [0, 3, 4, 7, 8, 9]
        mns_ids = self.create_masternodes(mns_idxs)
        time.sleep(5) # wait for enabling voter
        initialBlockCount = 200 + 4 * self.num_nodes + 1
        self.check_balances(MNS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 0)
        print("Checking block generation with no txs")
        self.check_no_txs()
        self.check_balances(NOTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 1)
        print("Checking block generation with PoW txs")
        self.check_pow_txs()
        self.check_balances(POWTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 3)
        print("Checking block generation with dPoS txs")
        self.check_dpos_txs()
        self.check_balances(DPOSTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 5)
        print("Checking block generation with PoW and dPoS txs")
        self.check_mix_txs()
        self.check_balances(MIXTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 6)
        print("Checking restart")
        self.check_restart()
        self.check_balances(MIXTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 6)
        print("Checking reindex")
        self.check_reindex()
        self.check_balances(MIXTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 6)

if __name__ == '__main__':
    dPoS_PositiveTest().main()
