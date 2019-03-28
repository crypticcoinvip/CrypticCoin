#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test masternodes heartbeat work
#

import sys
import time
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import \
    assert_equal, \
    assert_greater_than, \
    start_nodes, \
    stop_nodes, \
    connect_nodes_bi, \
    wait_bitcoinds

class MasternodesHeartbeatTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_option("--node-garaph-layout",
                          dest="node_garaph_layout",
                          default="1",
                          help="Setup node graph layout")

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir)

    def setup_chain(self):
        self.num_nodes = 4
        self.predpos_block_count = 201
        self.is_network_split = False
        super(MasternodesHeartbeatTest, self).setup_chain()
        sys.stdout.flush()

    def setup_network(self, split=False):
        self.nodes = self.setup_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        if len(self.nodes) > 3:
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
        if len(self.nodes) > 5:
            connect_nodes_bi(self.nodes, 3, 4)
            connect_nodes_bi(self.nodes, 4, 5)
        if len(self.nodes) > 7:
            connect_nodes_bi(self.nodes, 5, 6)
            connect_nodes_bi(self.nodes, 6, 7)
        self.sync_all()
        self.operators = []
        for n in range(self.num_nodes):
            self.operators.append(self.nodes[n].getnewaddress())
        self.stop_nodes()

    def start_nodes(self, args = []):
        assert_equal(len(self.nodes), 0)

        if len(args) == 0:
            args = [[]] * self.num_nodes

        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += self.get_nuparams_args()
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);

        if self.options.node_garaph_layout == "1":
            print("                      *")
            print("Nodes graph layout *<-*")
            print("                      *")
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 0, 2)
            connect_nodes_bi(self.nodes, 0, 3)
        elif self.options.node_garaph_layout == "2":
            print("                        *")
            print("Nodes graph layout *-*<")
            print("                        *")
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 1, 3)
        elif self.options.node_garaph_layout == "3":
            print("                      *")
            print("Check graph layout *<   >*")
            print("                      *")
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 0)

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def get_nuparams_args(self):
        return [
            '-nuparams=5ba81b19:' + str(self.predpos_block_count), # Overwinter
            '-nuparams=76b809bb:' + str(self.predpos_block_count)  # Suppling
        ]

    def create_masternodes(self):
        # Announce nodes
        idnodes = []
        for n in range(self.num_nodes):
            idnode = None
            if n == 0 or n == 3:
                owner = self.nodes[n].getnewaddress()
                collateral = self.nodes[n].getnewaddress()
                idnode = self.nodes[n].createraw_mn_announce([], {
                    "name": "node%d" % n,
                    "ownerAuthAddress": owner,
                    "operatorAuthAddress": self.operators[n],
                    "ownerRewardAddress": owner,
                    "collateralAddress": collateral
                })
            idnodes.append(idnode)

        # Sending some coins for auth
        self.nodes[0].sendtoaddress(self.operators[0], 5)
        self.nodes[3].sendtoaddress(self.operators[3], 5)
        self.sync_all()
        for node in self.nodes:
            node.generate(1) # +4
            self.sync_all()

        for node in self.nodes:
            for idnode in idnodes:
                if idnode:
                    assert_equal(node.dumpmns([idnode])[0]['status'], "announced")

        # Generate blocks for activation height
        for node in self.nodes:
            node.generate(10) # +40
            self.sync_all()
        for node in self.nodes:
            assert_equal(node.getblockcount(), self.predpos_block_count + 48 - 1)


        # Activate nodes
        self.nodes[0].createraw_mn_activate([])
        self.nodes[3].createraw_mn_activate([])
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        for node in self.nodes:
            for idnode in idnodes:
                if idnode:
                    assert_equal(node.dumpmns([idnode])[0]['status'], "active")
        for node in self.nodes:
            assert_equal(node.getblockcount(), self.predpos_block_count + 48)


    def run_test(self):
        self.start_nodes([["-masternode_operator="+self.operators[0]],
                          [],
                          [],
                          ["-masternode_operator="+self.operators[3]]])
        assert_equal(len(self.nodes), self.num_nodes)
        super(MasternodesHeartbeatTest, self).run_test()
        for node in self.nodes:
            node.generate(1) # +4
            self.sync_all()

        self.create_masternodes()

        for node in self.nodes:
            assert_equal(len(node.mn_listheartbeats()), 0)
        time.sleep(10)
        for node in self.nodes:
            assert_equal(len(node.mn_listheartbeats()), 2)

        for node in self.nodes:
            assert_equal(len(node.heartbeat_filter_masternodes("recently")), 2)
            assert_equal(len(node.heartbeat_filter_masternodes("stale")), 0)
            assert_equal(len(node.heartbeat_filter_masternodes("outdated")), 0)

        print "Done"



if __name__ == '__main__':
    MasternodesHeartbeatTest().main()
