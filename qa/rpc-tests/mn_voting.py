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

import time


class Mn(object):

    def __init__(self, node, name = ""):
        self.id = None
        self.name = name
        self.owner = node.getnewaddress()
        self.operator = node.getnewaddress()
        self.collateral = node.getnewaddress()


class MasternodesRpcVotingTest (BitcoinTestFramework):

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes
        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += ['-nuparams=76b809bb:200']

        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)

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
#        self.nodes[i].sendtoaddress(self.mns[i].operator, 5)
        return self.mns[i].id

    def activate_mn(self, i):
        return self.nodes[i].mn_activate([])

    def dismissvote_mn(self, frm, against, reason_code = 1, reason_desc = ""):
        return self.nodes[frm].mn_dismissvote([], {"against": self.mns[against].id, "reason_code": reason_code, "reason_desc": reason_desc})

    def finalizedismissvoting_mn(self, frm, against):
        return self.nodes[frm].mn_finalizedismissvoting([], {"against": self.mns[against].id })

    def dump_mn(self, i):
        return self.nodes[0].mn_list([ self.mns[i].id ], True)[0]

    def run_test (self):
        pp = pprint.PrettyPrinter(indent=4)

        self.start_nodes()
        print "Height at start: ", self.nodes[0].getblockcount()
        print "Announcing nodes"
        self.mns = [ Mn(self.nodes[i], "node"+str(i)) for i in range(self.num_nodes) ]
        for i in range(self.num_nodes):
            self.announce_mn(i)

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        for i in range(self.num_nodes):
            assert_equal(self.dump_mn(i)['status'], "announced")


        print "Restarting nodes"
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+self.mns[i].operator] for i in range(self.num_nodes) ] )
        print "Height at start: ", self.nodes[0].getblockcount()

        # Generate blocks for activation height
        for i in range(9):
            self.nodes[0].generate(1)
#        self.sync_all()    # May hang
        time.sleep(5)

        h = self.nodes[0].getblockcount()

        print "Autoactivation should happen here"
#        for i in range(self.num_nodes) :
#            self.activate_mn(i)
        self.nodes[0].generate(1)
#        self.sync_all()    # May hang

        while self.nodes[0].getblockcount() < h+1:
            print h+1
            time.sleep(0.5)

#        pp.pprint(self.nodes[0].mn_list([], True))
        for i in range(self.num_nodes):
            assert_equal(self.dump_mn(i)['status'], "active")


        print "Voting..."
        self.sync_all() # Important to accept blocks, otherwise other nodes may be not active!
        # Voting: node 0 against 1
        self.dismissvote_mn(0, 1)
        # Voting: node 1&2&3 against 0
        self.dismissvote_mn(1, 0)
        self.dismissvote_mn(2, 0)
        self.dismissvote_mn(3, 0)

        self.sync_all()
        time.sleep(4)
        self.nodes[0].generate(1)
        while self.nodes[0].getblockcount() < h+2:
            print h+2
            time.sleep(0.5)

        dump0 = self.dump_mn(0)
        assert_equal(dump0['mn']['counterVotesAgainst'], 3)
        assert_equal(dump0['mn']['counterVotesFrom'], 1)
        assert_equal(dump0['status'], "active")

        dump1 = self.dump_mn(1)
        assert_equal(dump1['mn']['counterVotesAgainst'], 1)
        assert_equal(dump1['mn']['counterVotesFrom'], 1)
        assert_equal(dump1['status'], "active")

        dump2 = self.dump_mn(2)
        assert_equal(dump2['mn']['counterVotesAgainst'], 0)
        assert_equal(dump2['mn']['counterVotesFrom'], 1)
        assert_equal(dump2['status'], "active")

        dump3 = self.dump_mn(3)
        assert_equal(dump3['mn']['counterVotesAgainst'], 0)
        assert_equal(dump3['mn']['counterVotesFrom'], 1)
        assert_equal(dump3['status'], "active")


        print "Restarting nodes, checking VerifyDB"
        self.stop_nodes()

        args = [[ "-checklevel=4" ]] * self.num_nodes
        for i in range(self.num_nodes):
            args[i] = args[i][:]
            args[i] += ["-masternode_operator="+self.mns[i].operator]

        self.start_nodes(args)

        self.nodes[0].generate(1)
        while self.nodes[0].getblockcount() < h+3:
            print h+3
            time.sleep(0.5)


        # Try to finalize voting against 1
        try:
            self.finalizedismissvoting_mn(0, 1)
        except JSONRPCException,e:
            errorString = e.error['message']
        assert("Dismissing quorum not reached" in errorString)

        # Finalize voting against 0
        print "Autofinalizing should happen"
#        self.finalizedismissvoting_mn(1, 0)

        self.nodes[1].generate(1)
        while self.nodes[0].getblockcount() < h+4:
            print h+4
            time.sleep(0.5)

        # +1 block for sure, cause round-robin
        self.nodes[1].generate(1)
        while self.nodes[0].getblockcount() < h+5:
            print h+5
            time.sleep(0.5)

        dump0 = self.dump_mn(0)
        assert_equal(dump0['mn']['counterVotesAgainst'], 0)
        assert_equal(dump0['mn']['counterVotesFrom'], 0)
        assert_equal(dump0['status'], "activated, dismissed")

        dump1 = self.dump_mn(1)
        assert_equal(dump1['mn']['counterVotesAgainst'], 0)
        assert_equal(dump1['mn']['counterVotesFrom'], 0)
        assert_equal(dump1['status'], "active")

        dump2 = self.dump_mn(2)
        assert_equal(dump2['mn']['counterVotesAgainst'], 0)
        assert_equal(dump2['mn']['counterVotesFrom'], 0)
        assert_equal(dump2['status'], "active")

        dump3 = self.dump_mn(3)
        assert_equal(dump3['mn']['counterVotesAgainst'], 0)
        assert_equal(dump3['mn']['counterVotesFrom'], 0)
        assert_equal(dump3['status'], "active")

        print "Done"


if __name__ == '__main__':
    MasternodesRpcVotingTest ().main ()
