#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test dPoS positive scenario
#

import sys
import time
from collections import OrderedDict
from dpos_base import dPoS_BaseTest
from test_framework.util import \
    assert_equal, \
    assert_greater_than

INITIAL_BALANCES    = [19372, 20624, 20624, 18747, 18372, 16874, 16874, 14997, 14997, 14997]
NOTXS_BALANCES      = [24997, 21250, 20624, 18747, 18372, 16874, 16874, 14997, 14997, 14997]
POWTXS_BALANCES     = [24997, 26874, 21249, 18747, 18372, 16874, 16874, 14997, 14997, 14997]
DPOSTXS_BALANCES    = [24997, 26874, 26874, 19372, 18372, 16874, 16874, 14997, 14997, 14997]
MIXTXS_BALANCES     = [24997, 26874, 26874, 24997, 18997, 16874, 16874, 14997, 14997, 14997]

class dPoS_PositiveTest(dPoS_BaseTest):
    def check_balances(self, balances):
#        for n in range(self.num_nodes):
#            print(int(self.nodes[n].getbalance() * 100))
        [assert_equal(int(self.nodes[n].getbalance() * 100), balances[n]) for n in range(self.num_nodes)]

    def check_no_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            self.nodes[n].generate(1)
            time.sleep(1)
            self.sync_all()
            blockCount = blockCount + 1
            self.check_nodes_block_count(blockCount)

    def check_pow_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            next_idx = n + 1 if n + 1 < self.num_nodes else 0
            zdst = [{ "address": self.nodes[next_idx].getnewaddress(), "amount": 4.4 }]
            self.nodes[n].sendtoaddress(self.operators[next_idx], 4.5)
            ztx = self.nodes[n].z_sendmany(self.operators[n], zdst, 1, 0.0001, False)
            time.sleep(1)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            assert_equal(ztx[0]["status"], "success")
            self.nodes[self.num_nodes - n - 1].generate(1)
            time.sleep(2)
            self.sync_all()
            blockCount = blockCount + 1
            self.check_nodes_block_count(blockCount)

    def check_dpos_txs(self):
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount()
            next_idx = n + 1 if n + 1 < self.num_nodes else 0
            toaddr = self.nodes[next_idx].getnewaddress()
            tx = self.create_transaction(n, toaddr, 20.2, True)
            zdst = [{ "address": toaddr, "amount": 3.3 }]
            self.nodes[n].sendtoaddress(self.operators[next_idx], 15.5)
            ztx = self.nodes[n].z_sendmany(self.operators[n], zdst, 1, 0.0001, True)
            time.sleep(2)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            assert_equal(ztx[0]["status"], "success")
            self.sync_all()
            for node in self.nodes:
                txs = {tx["hash"] for tx in node.list_instant_transactions()}
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
            time.sleep(3)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            self.sync_all()
            for node in self.nodes:
                txs = {tx["hash"] for tx in node.list_instant_transactions()}
                assert_equal(len(txs), 3)
                assert_equal(txs, {tx1, tx2, ztx[0]["result"]["txid"]})
            self.nodes[self.num_nodes - n - 1].generate(1)
            time.sleep(2)
            self.sync_all()
            blockCount = blockCount + 1
            self.check_nodes_block_count(blockCount)

    def check_restart(self):
        vblocks_before = [node.listdposviceblocks() for node in self.nodes]
        rdvotes_before = [node.listdposroundvotes() for node in self.nodes]
        txvotes_before = [node.listdpostxvotes() for node in self.nodes]
        self.stop_nodes()
        self.start_masternodes()
        self.connect_nodes()
        vblocks_after = [node.listdposviceblocks() for node in self.nodes]
        rdvotes_after = [node.listdposroundvotes() for node in self.nodes]
        txvotes_after = [node.listdpostxvotes() for node in self.nodes]
        print(vblocks_after)
        print(rdvotes_after)
        print(txvotes_after)
#        assert_equal(vblocks_before, vblocks_after)
#        assert_equal(rdvotes_before, rdvotes_after)
#        assert_equal(txvotes_before, txvotes_after)
        assert_equal(len(vblocks_before), len(vblocks_after))
        assert_equal(len(rdvotes_before), len(rdvotes_after))
        assert_equal(len(txvotes_before), len(txvotes_after))
#        assert_equal(OrderedDict(vblocks_before), OrderedDict(vblocks_after))
#        assert_equal(OrderedDict(rdvotes_before), OrderedDict(rdvotes_after))
#        assert_equal(OrderedDict(txvotes_before), OrderedDict(txvotes_after))


    def check_reindex(self):
        [assert_greater_than(len(node.listdposviceblocks()), 0) for node in self.nodes]
        [assert_greater_than(len(node.listdposroundvotes()), 0) for node in self.nodes]
        [assert_greater_than(len(node.listdpostxvotes()), 0) for node in self.nodes]
        self.stop_nodes()
        print(self.operators)
        self.start_masternodes([['-reindex']] * self.num_nodes)
        self.connect_nodes()
        [assert_equal(len(node.listdposviceblocks()), 0) for node in self.nodes]
        [assert_equal(len(node.listdposroundvotes()), 0) for node in self.nodes]
        [assert_equal(len(node.listdpostxvotes()), 0) for node in self.nodes]
        self.stop_nodes()
        print("wait")
        sys.stdin.readline()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 4)
        connect_nodes_bi(self.nodes, 4, 0)
        connect_nodes_bi(self.nodes, 5, 6)
        connect_nodes_bi(self.nodes, 6, 7)
        connect_nodes_bi(self.nodes, 7, 8)
        connect_nodes_bi(self.nodes, 8, 9)
        connect_nodes_bi(self.nodes, 9, 0)
        self.sync_nodes(0, 5)
        self.sync_nodes(5, 10)
        [assert_equal(len(node.listdposviceblocks()), 0) for node in self.nodes]
        [assert_equal(len(node.listdposroundvotes()), 0) for node in self.nodes]
        [assert_equal(len(node.listdpostxvotes()), 0) for node in self.nodes]
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 5.5)
        self.nodes[5].sendtoaddress(self.nodes[6].getnewaddress(), 5.5)
        time.sleep(2)
        self.nodes[2].generate(1)
        self.nodes[7].generate(1)
        time.sleep(2)
        self.sync_nodes(0, 5)
        self.sync_nodes(5, 10)
        vblocks = [node.listdposviceblocks() for node in self.nodes]
        rdvotes = [node.listdposroundvotes() for node in self.nodes]
        txvotes = [node.listdpostxvotes() for node in self.nodes]
        print(vblocks)
        print(rdvotes)
        print(txvotes)
#        self.start_masternodes()


    def run_test(self):
        super(dPoS_PositiveTest, self).run_test()
        print("Creating masternodes")
        mns = self.create_masternodes([0, 3, 4, 7, 8, 9])
        initialBlockCount = 200 + 10 * self.num_nodes + self.num_nodes + 1
        self.check_balances(INITIAL_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 0)
        print("Checking block generation with no txs")
        self.check_no_txs()
        self.check_balances(NOTXS_BALANCES)
        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 1)
        print("Checking block generation with PoW txs")
        self.check_pow_txs()
#        self.check_balances(POWTXS_BALANCES)
#        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 2)
#        print("Checking block generation with dPoS txs")
#        self.check_dpos_txs()
#        self.check_balances(DPOSTXS_BALANCES)
#        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 3)
#        print("Checking block generation with PoW and dPoS txs")
#        self.check_mix_txs()
#        self.check_balances(MIXTXS_BALANCES)
#        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 4)
        print("Checking restart")
        self.check_restart()
#        self.check_balances(MIXTXS_BALANCES)
#        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 4)
        print("Checking reindex")
        self.check_reindex()
#        self.check_balances(MIXTXS_BALANCES)
#        self.check_nodes_block_count(initialBlockCount + self.num_nodes * 4)

if __name__ == '__main__':
    dPoS_PositiveTest().main()
