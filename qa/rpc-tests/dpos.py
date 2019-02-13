#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test dPoS consensus work
#

import os
import sys
import time
import shutil
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import \
    assert_equal, \
    connect_nodes_bi, \
    sync_blocks, \
    start_nodes, \
    stop_nodes, \
    wait_bitcoinds, \
    initialize_chain_clean

_MASTERNODESDB_PATH = os.path.dirname(os.path.dirname(os.path.realpath(__file__))) + "/data/masternodes_dpos"
_WALLET_KEYS = [
    ("cSMhqrt1wStUuEnCyV97P3P9sUXVXVpn6Jb1iQ9VR5NPHYiMskkX", "tmXWM1dEwR8ANoc8iwwQ4pKzHvApXb65LsG"),
    ("cQvcpuFVeKHBmtCDHu1mXx8tU5b4HHw52FcUg62ZvALvaT6yrst9", "tmXbhS7QTA98nQhd3NMf7xe7ETLXiU4Dp41"),
    ("cPzSk8LGtysk8J5UEZDFdh7gVs8xGXd7PvSfgBoJtPKH4vnzH5kL", "tmYgqY2N9ykAqv9YqRsoPi16iojB9VuxBvY"),
    ("cQCtVNgf4J3CTTMtLqeXeeoFiZVCsSjv8gG7TRRD1xL69owZ1j9Z", "tmYDASd8j9bBLEVmiaU8tArTYmSVA9DJjXW")
]

class dPoS_Test(BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing dPoS_Test directory "+self.options.tmpdir)
        self.num_nodes = len(_WALLET_KEYS)
        self.is_network_split = False
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)

    def add_options(self, parser):
        parser.add_option("--node-garaph-layout",
                          dest="node_garaph_layout",
                          default="1",
                          help="Test dPoS work with connected nodes")

    def setup_network(self):
        super(dPoS_Test, self).setup_network(False)
        self.stop_nodes()

    def run_test(self):
        assert_equal(len(self.nodes), 0)
        assert_equal(len(_WALLET_KEYS), self.num_nodes)

        for n in range(self.num_nodes):
            mns_path = self.options.tmpdir + "/node{}/regtest/masternodes".format(n)
            shutil.rmtree(mns_path)
            shutil.copytree(_MASTERNODESDB_PATH, mns_path)

        if self.options.node_garaph_layout == "1":
            print("                      *")
            print("Nodes graph layout *<-*")
            print("                      *")
            self.start_nodes()
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 0, 2)
            connect_nodes_bi(self.nodes, 0, 3)
        elif self.options.node_garaph_layout == "2":
            print("                        *")
            print("Nodes graph layout *-*<")
            print("                        *")
            self.start_nodes()
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 1, 3)
        elif self.options.node_garaph_layout == "3":
            print("                      *")
            print("Check graph layout *<   >*")
            print("                      *")
            self.start_nodes()
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 0)

        self.check_initial_state()
        predpos_hashes = self.check_predpos_blocks()
        preblock_hashes = self.check_progenitor_blocks(predpos_hashes)
        pbvote_hashes = self.check_progenitor_votes(preblock_hashes)

        self.stop_nodes()

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes

        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += ['-nuparams=76b809bb:' + str(self.num_nodes)]
            if i != 1:
                args[i] += ['-masternode-operator=' + _WALLET_KEYS[i][1]]

        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);
        for n in range(self.num_nodes):
            wallet_key = _WALLET_KEYS[n]
            assert_equal(self.nodes[n].importprivkey(wallet_key[0]), wallet_key[1])

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def mine_blocks(self, predpos):
        hashes = []
        nodes = self.nodes if predpos else self.nodes[0:2]
        for node in nodes:
            hashes += node.generate(1)
            if predpos == True:
                self.sync_all()
        assert_equal(len(hashes), len(set(hashes)))
        for node in nodes:
            assert_equal(node.getblockcount(), self.num_nodes)
        return sorted(hashes, key=lambda h: h.decode("hex")[::-1])

    def check_initial_state(self):
        assert_equal(len(self.nodes), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 0)

    def check_predpos_blocks(self):
        predpos_hashes = self.mine_blocks(True)
        for node in self.nodes:
            assert_equal(len(node.listprogenitorblocks()), 0)
        return predpos_hashes

    def check_progenitor_blocks(self, predpos_hashes):
        preblock_hashes = self.mine_blocks(False)
        assert_equal(len(predpos_hashes), self.num_nodes)
        assert_equal(len(preblock_hashes), len(predpos_hashes) / 2)
        time.sleep(2)

        for node in self.nodes:
            pblocks = node.listprogenitorblocks()
            assert_equal(node.getblockcount(), self.num_nodes)
            assert_equal(len(pblocks), len(preblock_hashes))
            for idx, pblock in enumerate(pblocks):
                assert_equal(pblock["hash"], preblock_hashes[idx])

        return preblock_hashes

    def check_progenitor_votes(self, preblock_hashes):
        pbvote_hashes = []
        for node in self.nodes:
            pbvotes = node.listprogenitorvotes()
            assert_equal(node.getblockcount(), self.num_nodes)
            assert_equal(len(pbvotes), len(preblock_hashes) / 2)
            for pbvote in pbvotes:
                pbvote_hashes.append(pbvote["hash"])
#        self.nodes[0].generate(1)
#        time.sleep(2)
#        for node in self.nodes:
#            assert_equal(node.getblockcount(), self.num_nodes + 1)
        return pbvote_hashes

if __name__ == '__main__':
    dPoS_Test().main()
