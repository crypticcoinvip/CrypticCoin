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
    gather_inputs, \
    connect_nodes_bi, \
    sync_blocks, \
    start_nodes, \
    stop_nodes, \
    wait_bitcoinds, \
    initialize_chain_clean

_COINBASE_MATURITY = 100
_MASTERNODESDB_PATH = os.path.dirname(os.path.dirname(os.path.realpath(__file__))) + "/data/masternodes_dpos"
_WALLET_KEYS = [
    ("cSMhqrt1wStUuEnCyV97P3P9sUXVXVpn6Jb1iQ9VR5NPHYiMskkX", "tmXWM1dEwR8ANoc8iwwQ4pKzHvApXb65LsG"),
    ("cQvcpuFVeKHBmtCDHu1mXx8tU5b4HHw52FcUg62ZvALvaT6yrst9", "tmXbhS7QTA98nQhd3NMf7xe7ETLXiU4Dp41"),
    ("cPzSk8LGtysk8J5UEZDFdh7gVs8xGXd7PvSfgBoJtPKH4vnzH5kL", "tmYgqY2N9ykAqv9YqRsoPi16iojB9VuxBvY"),
    ("cQCtVNgf4J3CTTMtLqeXeeoFiZVCsSjv8gG7TRRD1xL69owZ1j9Z", "tmYDASd8j9bBLEVmiaU8tArTYmSVA9DJjXW")
]

class dPoS_BaseTest(BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing dPoS Test directory "+self.options.tmpdir)
        self.num_nodes = len(_WALLET_KEYS)
        self.predpos_block_count = _COINBASE_MATURITY + self.num_nodes
        self.is_network_split = False
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)
        sys.stdout.flush()

    def add_options(self, parser):
        parser.add_option("--node-garaph-layout",
                          dest="node_garaph_layout",
                          default="1",
                          help="Test dPoS work with connected nodes")

    def setup_network(self):
        super(dPoS_BaseTest, self).setup_network(False)
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

        assert_equal(len(self.nodes), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 0)

        predpos_hashes = self.mine_blocks(True, self.predpos_block_count)
        for node in self.nodes:
            assert_equal(len(node.listprogenitorblocks()), 0)
        assert_equal(len(predpos_hashes), self.predpos_block_count)

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes

        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += [
                '-nuparams=5ba81b19:' + str(self.predpos_block_count), # Overwinter
                '-nuparams=76b809bb:' + str(self.predpos_block_count)  # Suppling
            ]
            args[i] += ['-masternode-operator=' + _WALLET_KEYS[i][1]]

        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);
        for n in range(self.num_nodes):
            wallet_key = _WALLET_KEYS[n]
            assert_equal(self.nodes[n].importprivkey(wallet_key[0]), wallet_key[1])

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def mine_blocks(self, predpos, count):
        hashes = []
        nodes = self.nodes if predpos else self.nodes[0:2]
        assert_equal(count % len(nodes), 0)

        for i in range(count / len(nodes)):
            for node in nodes:
                hashes += node.generate(1)
                if predpos == True:
                    self.sync_all()
        assert_equal(len(hashes), len(set(hashes)))
        for node in nodes:
            assert_equal(node.getblockcount(), count)
        return sorted(hashes, key=lambda h: h.decode("hex")[::-1])

    def create_transaction(self, node_idx, to_address, amount, instantly):
        node = self.nodes[node_idx]
        (total_in, inputs) = gather_inputs(node, amount)
        outputs = { to_address : amount - amount * 0.0002 }
        return node.createrawtransaction(inputs, outputs, 0, node.getblockcount() + 21, instantly)

assert(__name__ != '__main__')
