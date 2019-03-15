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


class MasternodesRpcRevertTest (BitcoinTestFramework):

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
#        self.nodes[i].sendtoaddress(self.mns[i].operator, 5)
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

    def run_test (self):
        pp = pprint.PrettyPrinter(indent=4)

        self.num_nodes = 3
        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 0)
        # Node #3 not started!


        print "Announce nodes 0,1,2"
        self.mns = [ Mn(self.nodes[i], "node"+str(i)) for i in range(self.num_nodes) ]
        for i in range(self.num_nodes):
            self.announce_mn(i)

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        for i in range(self.num_nodes):
            assert_equal(self.dump_mn(i)['status'], "announced")


        print "Nodes announced, restarting nodes (node #2 disconnected)"
        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+self.mns[i].operator] for i in range(self.num_nodes) ] )
        connect_nodes_bi(self.nodes, 0, 1)

        # Generate blocks for activation height
        self.nodes[0].generate(9)
        sync_blocks([self.nodes[0], self.nodes[1]])

        # Activate nodes 0+1. (autoactivation should happen here)
#        self.activate_mn(0)
#        self.activate_mn(1)
#        sync_mempools([self.nodes[0], self.nodes[1]])
        self.nodes[0].generate(1)
        sync_blocks([self.nodes[0], self.nodes[1]])
        time.sleep(4)

        assert_equal(self.dump_mn(0)['status'], "active")
        assert_equal(self.dump_mn(1)['status'], "active")
        assert_equal(self.dump_mn(2)['status'], "announced")


        print "Nodes 0,1 activated. Nodes 0 voting against #2 (only announced, offline)"
        self.dismissvote_mn(0, 2)
#        self.dismissvote_mn(1, 2) # !!! now, autofinalize may happen
#        sync_mempools([self.nodes[0], self.nodes[1]])
        time.sleep(4)
        self.nodes[0].generate(1) #(total +11)
        sync_blocks([self.nodes[0], self.nodes[1]])
        time.sleep(4)

#        pp.pprint(self.nodes[0].mn_list([], True))

        # Check dimissvoterecall
        assert_equal(self.nodes[0].mn_list([self.mns[0].id], True)[0]['mn']['counterVotesFrom'], 1)
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['mn']['counterVotesAgainst'], 1)
        self.dismissvoterecall_mn(0, 2)
        self.nodes[0].generate(1) #(total +12)
#        time.sleep(4)
        assert_equal(self.nodes[0].mn_list([self.mns[0].id], True)[0]['mn']['counterVotesFrom'], 0)
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['mn']['counterVotesAgainst'], 0)
        self.dismissvote_mn(0, 2)
        self.nodes[0].generate(1) #(total +13)
#        time.sleep(4)
        assert_equal(self.nodes[0].mn_list([self.mns[0].id], True)[0]['mn']['counterVotesFrom'], 1)
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['mn']['counterVotesAgainst'], 1)


        print "Node 0(1??) autofinalize dismiss voting against #2 (offline)"
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['status'], "announced")
#        self.finalizedismissvoting_mn(0, 2)
        self.dismissvote_mn(1, 2)
        time.sleep(4) #
        self.nodes[0].generate(1) #(total +14)
        sync_blocks([self.nodes[0], self.nodes[1]])
        time.sleep(4)
        assert_equal(self.nodes[0].mn_list([self.mns[0].id], True)[0]['mn']['counterVotesFrom'], 1)
        assert_equal(self.nodes[0].mn_list([self.mns[1].id], True)[0]['mn']['counterVotesFrom'], 1)
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['mn']['counterVotesAgainst'], 2)
        # Autofinalize should happen here
        self.nodes[0].generate(1) #+15
        sync_blocks([self.nodes[0], self.nodes[1]])
        self.nodes[0].generate(1) #+16
        sync_blocks([self.nodes[0], self.nodes[1]])
        time.sleep(4)


        # Check that node #2 dismissed and counters decreased
        assert_equal(self.nodes[0].mn_list([self.mns[0].id], True)[0]['mn']['counterVotesFrom'], 0)
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['mn']['counterVotesAgainst'], 0)
        assert_equal(self.nodes[0].mn_list([self.mns[2].id], True)[0]['status'], "announced, dismissed")
        sync_blocks([self.nodes[0], self.nodes[1]])


        print "Restarting nodes. Node #2 mines forward, reverting dismissed status and all votes"
        nodesOld = self.nodes[0].mn_list([], True)
        votesOld = self.nodes[0].mn_listdismissvotes()

        self.stop_nodes()
        self.start_nodes([[ "-masternode_operator="+self.mns[i].operator] for i in range(self.num_nodes) ] )

        nodesNew = self.nodes[0].mn_list([], True)
        votesNew = self.nodes[0].mn_listdismissvotes()
        assert_equal(nodesOld, nodesNew)
        assert_equal(votesOld, votesNew)
#        pp.pprint(nodesOld)
#        pp.pprint(nodesNew)
#        pp.pprint(votesOld)
#        pp.pprint(votesNew)

        assert_equal(self.nodes[2].mn_list([self.mns[0].id])[0]['status'], "announced")
        assert_equal(self.nodes[2].mn_list([self.mns[1].id])[0]['status'], "announced")
        assert_equal(self.nodes[2].mn_list([self.mns[2].id])[0]['status'], "announced")
        self.nodes[2].generate(18) #(total +17)
        connect_nodes_bi(self.nodes, 0, 2)
        sync_blocks([self.nodes[0], self.nodes[2]])
        time.sleep(4)
        assert_equal(self.nodes[0].mn_list([self.mns[0].id])[0]['status'], "announced")
        assert_equal(self.nodes[0].mn_list([self.mns[1].id])[0]['status'], "announced")
        assert_equal(self.nodes[0].mn_list([self.mns[2].id])[0]['status'], "active") # cause autoactivation


        print "Resigning node 1, reverting node #2 work"
        collateral1out = self.nodes[1].getnewaddress()
        self.nodes[1].mn_resign(self.mns[1].id, collateral1out)
        self.nodes[1].generate(5) #(total +18??)

        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks([self.nodes[0], self.nodes[1]])
        assert_equal(self.nodes[0].mn_list([self.mns[0].id])[0]['status'], "active")
        assert_equal(self.nodes[0].mn_list([self.mns[1].id])[0]['status'], "activated, resigned")
        assert_equal(self.nodes[0].mn_list([self.mns[2].id])[0]['status'], "announced, dismissed")


        print "Restarting all nodes (+ #3). Node #3 reverts to 'no masternodes at all'"
        self.stop_nodes()
        self.num_nodes = 4
        args = [[]] * self.num_nodes
        for i in range(3): args[i] = [ "-masternode_operator="+self.mns[i].operator ]
        self.start_nodes(args)
        self.nodes[3].generate(30)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        sync_blocks(self.nodes)
        assert_equal(len(self.nodes[0].mn_listactive()), 0)

        print "Done"


if __name__ == '__main__':
    MasternodesRpcRevertTest ().main ()
