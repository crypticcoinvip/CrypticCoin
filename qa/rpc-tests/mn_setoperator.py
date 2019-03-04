#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, \
    initialize_chain, initialize_chain_clean, start_nodes, start_node, connect_nodes_bi, \
    stop_nodes, sync_blocks, sync_mempools, wait_and_assert_operationid_status, \
    wait_bitcoinds

from decimal import Decimal
import pprint

class MasternodesRpcAnnounceTest (BitcoinTestFramework):

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes
        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += ['-nuparams=76b809bb:200']

        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def setup_network(self, split=False):
        self.num_nodes = 2
        self.is_network_split=False

    def run_test (self):
        pp = pprint.PrettyPrinter(indent=4)

        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)

        # Announce node0
        owner0 = self.nodes[0].getnewaddress()
        operator0 = self.nodes[0].getnewaddress()
        collateral0 = self.nodes[0].getnewaddress()
        operatorReward0 = self.nodes[0].getnewaddress()
        operatorRewardRatio0 = 0.25

        idnode0 = self.nodes[0].createraw_mn_announce([], {
            "name": "node0",
            "ownerAuthAddress": owner0,
            "operatorAuthAddress": operator0,
            "ownerRewardAddress": owner0,
            "operatorRewardAddress": operatorReward0,
            "operatorRewardRatio": operatorRewardRatio0,
            "collateralAddress": collateral0
        })

        # Sending some coins for auth
        self.nodes[0].sendtoaddress(operator0, 5)
        self.nodes[0].sendtoaddress(owner0, 5)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].dumpmns([idnode0])[0]['status'], "announced")

        # Generate blocks for activation height
        self.nodes[0].generate(10)
        self.sync_all()

        # Restarting nodes disconnected
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+operator0, "-masternode_owner="+owner0 ], []])

        # Check old operator info
        mn = self.nodes[0].dumpmns([idnode0])[0]['mn']
        assert_equal(mn['operatorAuthAddress'], operator0)
        assert_equal(mn['operatorRewardAddress']['addresses'][0], operatorReward0)
        assert_equal(mn['operatorRewardRatio'], operatorRewardRatio0)

        # Set new operator
        operator0new = self.nodes[0].getnewaddress()
        operatorReward0new = self.nodes[0].getnewaddress()
        operatorRewardRatio0new = 0.5

        self.nodes[0].createraw_set_operator_reward([], {
            "operatorAuthAddress": operator0new,
            "operatorRewardAddress": operatorReward0new,
            "operatorRewardRatio": operatorRewardRatio0new
        })
        self.nodes[0].generate(1)

        # Check new operator info
        mn = self.nodes[0].dumpmns([idnode0])[0]['mn']
        assert_equal(mn['operatorAuthAddress'], operator0new)
        assert_equal(mn['operatorRewardAddress']['addresses'][0], operatorReward0new)
        assert_equal(mn['operatorRewardRatio'], operatorRewardRatio0new)

        # Restarting nodes disconnected
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+operator0new, "-masternode_owner="+owner0 ], []])

        # Activate node from NEW operator
        self.nodes[0].sendtoaddress(operator0new, 5)
        self.nodes[0].generate(1)
        self.nodes[0].createraw_mn_activate([])
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].dumpmns([idnode0])[0]['status'], "active")

        # Reverting state of node0 by mining at node1
        self.nodes[1].generate(5)
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks([self.nodes[0], self.nodes[1]])

        # Check old operator info
        mn = self.nodes[0].dumpmns([idnode0])[0]['mn']
        assert_equal(mn['operatorAuthAddress'], operator0)
        assert_equal(mn['operatorRewardAddress']['addresses'][0], operatorReward0)
        assert_equal(mn['operatorRewardRatio'], operatorRewardRatio0)

        assert_equal(self.nodes[0].dumpmns([idnode0])[0]['status'], "announced")

        print "Done"


if __name__ == '__main__':
    MasternodesRpcAnnounceTest ().main ()
