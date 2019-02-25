#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test masternodes heartbeat work
#

import sys
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import \
    assert_equal, \
    assert_greater_than, \
    initialize_chain, \
    initialize_chain_clean, \
    start_nodes, \
    start_node, \
    connect_nodes_bi, \
    stop_nodes, \
    sync_blocks, \
    sync_mempools, \
    wait_and_assert_operationid_status, \
    wait_bitcoinds

#_WALLET_KEYS = [
#    ("cSMhqrt1wStUuEnCyV97P3P9sUXVXVpn6Jb1iQ9VR5NPHYiMskkX", "tmXWM1dEwR8ANoc8iwwQ4pKzHvApXb65LsG"),
#    ("cQvcpuFVeKHBmtCDHu1mXx8tU5b4HHw52FcUg62ZvALvaT6yrst9", "tmXbhS7QTA98nQhd3NMf7xe7ETLXiU4Dp41"),
#    ("cPzSk8LGtysk8J5UEZDFdh7gVs8xGXd7PvSfgBoJtPKH4vnzH5kL", "tmYgqY2N9ykAqv9YqRsoPi16iojB9VuxBvY"),
#    ("cQCtVNgf4J3CTTMtLqeXeeoFiZVCsSjv8gG7TRRD1xL69owZ1j9Z", "tmYDASd8j9bBLEVmiaU8tArTYmSVA9DJjXW")
#]

class MasternodesHeartbeatTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_option("--node-garaph-layout",
                          dest="node_garaph_layout",
                          default="1",
                          help="Setup node graph layout")

    def setup_chain(self):
        self.num_nodes = 4
        self.predpos_block_count = 204
        self.is_network_split = False
        super(MasternodesHeartbeatTest, self).setup_chain()
        sys.stdout.flush()

    def setup_network(self):
        super(MasternodesHeartbeatTest, self).setup_network(False)
        self.stop_nodes()

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes

        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += self.get_nuparams_args()
            #args[i] += ['-masternode-operator=' + _WALLET_KEYS[i][1]]

        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def get_nuparams_args(self):
        return [
            '-nuparams=5ba81b19:' + str(self.predpos_block_count), # Overwinter
            '-nuparams=76b809bb:' + str(self.predpos_block_count)  # Suppling
        ]

    def run_test(self):
        assert_equal(len(self.nodes), 0)
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
        super(MasternodesHeartbeatTest, self).run_test()
        for node in self.nodes:
            node.generate(1)
            self.sync_all()
        for node in self.nodes:
            assert_equal(node.getblockcount(), self.predpos_block_count)

        # Announce node0
        owner0 = self.nodes[0].getnewaddress()
        operator0 = self.nodes[0].getnewaddress()
        collateral0 = self.nodes[0].getnewaddress()

        idnode0 = self.nodes[0].createraw_mn_announce([], {
            "name": "node0",
            "ownerAuthAddress": owner0,
            "operatorAuthAddress": operator0,
            "ownerRewardAddress": owner0,
            "collateralAddress": collateral0
        })

        # Announce node3
        owner3 = self.nodes[3].getnewaddress()
        operator3 = self.nodes[3].getnewaddress()
        collateral3 = self.nodes[3].getnewaddress()

        idnode3 = self.nodes[3].createraw_mn_announce([], {
            "name": "node3",
            "ownerAuthAddress": owner3,
            "operatorAuthAddress": operator3,
            "ownerRewardAddress": owner3,
            "collateralAddress": collateral3
        })

        # Sending some coins for auth
        self.nodes[0].sendtoaddress(operator0, 5)
        self.nodes[3].sendtoaddress(operator3, 5)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].dumpmns([idnode0])[0]['status'], "announced")
        assert_equal(self.nodes[0].dumpmns([idnode3])[0]['status'], "announced")


        # Generate blocks for activation height
        self.nodes[0].generate(10)
        self.sync_all()

        # Restarting nodes
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+operator0 ], [], [], [ "-masternode_operator="+operator3 ]])

        # Activate nodes
        self.nodes[0].createraw_mn_activate([])
        self.nodes[3].createraw_mn_activate([])
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].dumpmns([idnode0])[0]['status'], "active")
        assert_equal(self.nodes[0].dumpmns([idnode3])[0]['status'], "active")

        print "Done"



if __name__ == '__main__':
    MasternodesHeartbeatTest().main()
