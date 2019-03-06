#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test dPoS p2p messages
#

import os
import sys
import time
from time import sleep
from dpos_base import dPoS_BaseTest
from test_framework.util import \
    assert_equal, \
    assert_not_equal, \
    assert_greater_than, \
    connect_nodes_bi

class dPoS_p2pMessagesTest(dPoS_BaseTest):
    def run_test(self):
        super(dPoS_p2pMessagesTest, self).run_test()
        mns = self.create_masternodes([1, 2, 3, 4])
        self.stop_nodes()
        self.start_masternodes()
        # First group (4 masternodes)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 4)
        connect_nodes_bi(self.nodes, 4, 0)
        # Second group (0 masternodes)
        connect_nodes_bi(self.nodes, 5, 6)
        connect_nodes_bi(self.nodes, 6, 7)
        connect_nodes_bi(self.nodes, 7, 8)
        connect_nodes_bi(self.nodes, 8, 9)
        connect_nodes_bi(self.nodes, 9, 5)
        self.sync_nodes(0, 5)
        self.sync_nodes(5, 10)
        time.sleep(15)
        [assert_equal(len(node.dpos_listviceblocks()), 0) for node in self.nodes]
        [assert_equal(len(node.dpos_listroundvotes()), 0) for node in self.nodes]
        [assert_equal(len(node.dpos_listtxvotes()), 0) for node in self.nodes]
        tx1 = self.create_transaction(1, self.nodes[9].getnewaddress(), 4.4, True)
        tx2 = self.create_transaction(6, self.nodes[0].getnewaddress(), 4.4, True)
        time.sleep(2)
        txs1 = self.nodes[2].list_instant_transactions()
        txs2 = self.nodes[7].list_instant_transactions()
        assert_equal(len(txs1), 1)
        assert_equal(len(txs2), 0)
        assert_equal(txs1[0]["hash"], tx1)
        self.nodes[3].generate(1)
        self.nodes[8].generate(1)
        time.sleep(2)
        self.sync_nodes(0, 5)
        self.sync_nodes(5, 10)
        vblocks = [node.dpos_listviceblocks() for node in self.nodes]
        rdvotes = [node.dpos_listroundvotes() for node in self.nodes]
        txvotes = [node.dpos_listtxvotes() for node in self.nodes]
        sys.stdin.readline()
        vblocks_left = vblocks[0:len(vblocks)/2]
        vblocks_right = vblocks[len(vblocks)/2:]
        assert_equal(len(vblocks_left), len(vblocks_right))
        rdvotes_left = rdvotes[0:len(rdvotes)/2]
        rdvotes_right = rdvotes[len(rdvotes)/2:]
        assert_equal(len(rdvotes_left), len(rdvotes_right))
        txvotes_left = vblocks[0:len(txvotes)/2]
        txvotes_right = vblocks[len(rdvotes)/2:]
        assert_equal(len(txvotes_left), len(txvotes_right))
        assert_not_equal(vblocks_left, vblocks_right)
        assert_not_equal(rdvotes_left, rdvotes_right)
        assert_not_equal(txvotes_left, txvotes_right)
        [assert_not_equal(len(x), 0) for x in vblocks_left]
        [assert_not_equal(len(x), 0) for x in vblocks_right]
        [assert_not_equal(len(x), 0) for x in rdvotes_left]
        [assert_equal(len(x), 0) for x in rdvotes_right]
        [assert_not_equal(len(x), 0) for x in txvotes_left]
        [assert_not_equal(len(x), 0) for x in txvotes_right]


if __name__ == '__main__':
    dPoS_p2pMessagesTest().main()
