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

import time
import pprint

class Mn(object):

    def __init__(self, node, name = ""):
        self.id = None
        self.name = name
        self.owner = node.getnewaddress()
        self.operator = node.getnewaddress()
        self.collateral = node.getnewaddress()


class MasternodesRpcTeamRewardTest (BitcoinTestFramework):

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
        self.num_nodes = 4
        self.is_network_split=False

    def announce_mn(self, i):
        self.mns[i].id = self.nodes[i].mn_announce([], {
            "name": self.mns[i].name,
            "ownerAuthAddress": self.mns[i].owner,
            "operatorAuthAddress": self.mns[i].operator,
            "ownerRewardAddress": self.mns[i].owner,
            "collateralAddress": self.mns[i].collateral
        })
        # Sending some coins for auth
        self.nodes[i].sendtoaddress(self.mns[i].operator, 5)
        return self.mns[i].id

    def activate_mn(self, i):
        return self.nodes[i].mn_activate([])

    def dismissvote_mn(self, frm, against, reason_code = 1, reason_desc = ""):
        return self.nodes[frm].mn_dismissvote([], {"against": self.mns[against].id, "reason_code": reason_code, "reason_desc": reason_desc})

    def dismissvoterecall_mn(self, frm, against, reason_code = 1, reason_desc = ""):
        return self.nodes[frm].mn_dismissvoterecall([], {"against": self.mns[against].id })

    def finalizedismissvoting_mn(self, frm, against):
        return self.nodes[frm].mn_finalizedismissvoting([], {"against": self.mns[against].id })

    def dump_mn(self, i):
        return self.nodes[i].mn_list([ self.mns[i].id ], True)[0]

    def gettipcoinbase(self, i):
        height = self.nodes[i].getblockcount()
        block = self.nodes[i].getblock(str(height))
        tx0hash = block['tx'][0]
        return self.nodes[i].decoderawtransaction(self.nodes[i].getrawtransaction(tx0hash))

    def run_test (self):
        pp = pprint.PrettyPrinter(indent=4)

        # We need consensus.dpos.nTeamSize (in chainparams.cpp) for test execution
        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        time.sleep(2)


        print "Announce nodes"
        self.mns = [ Mn(self.nodes[i], "node"+str(i)) for i in range(self.num_nodes) ]
        for i in range(self.num_nodes):
            self.announce_mn(i)

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        for i in range(self.num_nodes):
            assert_equal(self.dump_mn(i)['status'], "announced")


        print "Nodes announced, restarting nodes"
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+self.mns[i].operator] for i in range(self.num_nodes) ] )
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        time.sleep(4)


        # Generate blocks for activation height
        self.nodes[0].generate(10)
        self.sync_all()

        # Activate nodes 0+1
        self.activate_mn(0)
        self.activate_mn(1)
        self.activate_mn(2)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        assert_equal(self.dump_mn(0)['status'], "active")
        assert_equal(self.dump_mn(1)['status'], "active")
        assert_equal(self.dump_mn(2)['status'], "active")
        assert_equal(self.dump_mn(3)['status'], "announced")

        self.activate_mn(3)
        self.nodes[3].generate(1)
        self.sync_all()
        assert_equal(self.dump_mn(3)['status'], "active")

        # Just for sure that dpos has started after activation
        time.sleep(4)

        # should be old PoW coinbase
        height = self.nodes[0].getblockcount()
        pow = self.gettipcoinbase(0)
        assert_true(len(pow['vout']) == 1)
        assert_true(pow['vout'][0]['value'] >= 6.25)

        self.nodes[0].generate(1)

        while height == self.nodes[0].getblockcount():
            print height
            time.sleep(1)

        # should be new dPoS coinbase
        dpos = self.gettipcoinbase(0)
        assert_true(len(dpos['vout']) == 5)
        assert_true(dpos['vout'][0]['value'] == 3.125)
        assert_true(dpos['vout'][1]['value'] == 0.78125)
        assert_true(dpos['vout'][2]['value'] == 0.78125)
        assert_true(dpos['vout'][3]['value'] == 0.78125)
        assert_true(dpos['vout'][4]['value'] == 0.78125)

        print "Done"


if __name__ == '__main__':
    MasternodesRpcTeamRewardTest ().main ()
