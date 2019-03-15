#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_true, assert_greater_than, \
    initialize_chain, initialize_chain_clean, start_nodes, start_node, connect_nodes_bi, \
    stop_nodes, sync_blocks, sync_mempools, wait_and_assert_operationid_status, \
    wait_bitcoinds

from decimal import Decimal
import pprint
import time

class MasternodesRpcBasicTest (BitcoinTestFramework):

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes
        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += ['-nuparams=76b809bb:200']

        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);
        connect_nodes_bi(self.nodes, 0, 1)

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def setup_network(self, split=False):
        self.num_nodes = 2
        self.is_network_split=False

    def run_test (self):
        pp = pprint.PrettyPrinter(indent=4)

        self.start_nodes()

        # Announce node0
        owner0 = self.nodes[0].getnewaddress()
        operator0 = self.nodes[0].getnewaddress()
        collateral0 = self.nodes[0].getnewaddress()

        idnode0 = self.nodes[0].mn_announce([], {
            "name": "node0",
            "ownerAuthAddress": owner0,
            "operatorAuthAddress": operator0,
            "ownerRewardAddress": owner0,
            "collateralAddress": collateral0
        })


        # Announce node1
        owner1 = self.nodes[1].getnewaddress()
        operator1 = self.nodes[1].getnewaddress()
        collateral1 = self.nodes[1].getnewaddress()

        idnode1 = self.nodes[1].mn_announce([], {
            "name": "node1",
            "ownerAuthAddress": owner1,
            "operatorAuthAddress": operator1,
            "ownerRewardAddress": owner1,
            "collateralAddress": collateral1
        })


        # Sending some coins for auth
#        self.nodes[0].sendtoaddress(operator0, 0.000001)
#        self.nodes[1].sendtoaddress(operator1, 5)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].mn_list([idnode0])[0]['status'], "announced")
        assert_equal(self.nodes[0].mn_list([idnode1])[0]['status'], "announced")

        # Check locked collateral:
        # (total-11) is Ok, but (total-10) should fail! (cause locked collateral)
        self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), self.nodes[0].getbalance() - 11)
        try:
            self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), self.nodes[0].getbalance() - 10)
        except JSONRPCException,e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)


        # Restarting nodes
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+operator0 ], [ "-masternode_operator="+operator1 ]])

        # Generate blocks for activation height
        self.nodes[0].generate(10)
        self.sync_all()
        time.sleep(4)

        # Autoactivation should happen
#        act0id = self.nodes[0].mn_activate([])
#        act1id = self.nodes[1].mn_activate([])

        self.nodes[0].generate(1)
        self.sync_all()

        # Voting against each other
        self.nodes[0].mn_dismissvote([], {"against": idnode1, "reason_code": 1, "reason_desc": "go away!"})
        self.nodes[1].mn_dismissvote([], {"against": idnode0, "reason_code": 1, "reason_desc": "noooo"})
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        node0dump = self.nodes[0].mn_list([idnode0], True)
        assert_equal(node0dump[0]['mn']['counterVotesAgainst'], 1)
        assert_equal(node0dump[0]['mn']['counterVotesFrom'], 1)

        node1dump = self.nodes[0].mn_list([idnode1], True)
        assert_equal(node1dump[0]['mn']['counterVotesAgainst'], 1)
        assert_equal(node1dump[0]['mn']['counterVotesFrom'], 1)


        # Resign node0
        collateral0out = self.nodes[0].getnewaddress()

        self.nodes[0].mn_resign(idnode0, collateral0out)
        self.nodes[0].generate(1)
        self.sync_all()
        node0dump = self.nodes[0].mn_list([idnode0], True)
        assert_equal(node0dump[0]['status'], "activated, resigned")
        assert_equal(node0dump[0]['mn']['counterVotesAgainst'], 0)
        assert_equal(node0dump[0]['mn']['counterVotesFrom'], 0)
        node1dump = self.nodes[0].mn_list([idnode1], True)
        assert_equal(node1dump[0]['status'], "active")
        assert_equal(node1dump[0]['mn']['counterVotesAgainst'], 0)
        assert_equal(node1dump[0]['mn']['counterVotesFrom'], 0)

        print "Done"


if __name__ == '__main__':
    MasternodesRpcBasicTest ().main ()
