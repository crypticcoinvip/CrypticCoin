#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test proper accounting with malleable transactions
#

import sys
import time
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import \
    assert_equal, \
    connect_nodes_bi, \
    sync_blocks, \
    start_nodes, \
    stop_nodes, \
    wait_bitcoinds, \
    initialize_chain_clean


class SendHeartbeatTest(BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing SendHeartbeatTest directory "+self.options.tmpdir)
        self.num_nodes = 4
        self.is_network_split=False
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)

    def setup_network(self):
        super(SendHeartbeatTest, self).setup_network(False)
        self.stop_nodes()

    def run_test(self):
        assert_equal(len(self.nodes), 0)

        print("                      *")
        print("Check graph layout *<-*")
        print("                      *")

        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 0, 3)

        # Check that nodes successfully synchronizied
        assert_equal(len(self.nodes), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 0)
        for node in self.nodes:
            node.generate(1)
            self.sync_all()

        self.check_hearbeat_messages()
        self.stop_nodes()

        print("                        *")
        print("Check graph layout *-*<")
        print("                        *")

        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 1, 3)
        self.check_hearbeat_messages()
        self.stop_nodes()

        print("                      *")
        print("Check graph layout *<   >*")
        print("                      *")

        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        self.check_hearbeat_messages()
        self.stop_nodes()


    def start_nodes(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir);

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def check_hearbeat_messages(self):
        assert_equal(len(self.nodes), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.getblockcount(), self.num_nodes)
        for node in self.nodes:
            assert_equal(int(node.read_heartbeat_message()), 0)

        # Check that all nodes recieved heartbeat
        current_ts = int(time.time())
        self.nodes[0].send_heartbeat_message(current_ts)
        time.sleep(1)
        for n in range(self.num_nodes):
            print(n)
            assert_equal(int(self.nodes[n].read_heartbeat_message()), current_ts)


#        self.nodes[2].generate(1)  # Mine another block to make sure we sync
#        sync_blocks(self.nodes)

#        for i in range(4):
#            assert_equal(self.nodes[i].getbalance(), starting_balance)
#            self.nodes[i].getnewaddress("")  # bug workaround, coins generated assigned to first getnewaddress!

#        mining_reward = 10
#        starting_balance = mining_reward * 25

#        for i in range(4):
#            assert_equal(self.nodes[i].getbalance(), starting_balance)
#            self.nodes[i].getnewaddress("")  # bug workaround, coins generated assigned to first getnewaddress!

#        # Coins are sent to node1_address
#        node1_address = self.nodes[1].getnewaddress("")

#        # First: use raw transaction API to send (starting_balance - (mining_reward - 2)) BTC to node1_address,
#        # but don't broadcast:
#        (total_in, inputs) = gather_inputs(self.nodes[0], (starting_balance - (mining_reward - 2)))
#        change_address = self.nodes[0].getnewaddress("")
#        outputs = {}
#        outputs[change_address] = (mining_reward - 2)
#        outputs[node1_address] = (starting_balance - (mining_reward - 2))
#        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
#        doublespend = self.nodes[0].signrawtransaction(rawtx)
#        assert_equal(doublespend["complete"], True)

#        # Create two transaction from node[0] to node[1]; the
#        # second must spend change from the first because the first
#        # spends all mature inputs:
#        txid1 = self.nodes[0].sendfrom("", node1_address, (starting_balance - (mining_reward - 2)), 0)
#        txid2 = self.nodes[0].sendfrom("", node1_address, 5, 0)

#        # Have node0 mine a block:
#        if (self.options.mine_block):
#            self.nodes[0].generate(1)
#            sync_blocks(self.nodes[0:2])

#        tx1 = self.nodes[0].gettransaction(txid1)
#        tx2 = self.nodes[0].gettransaction(txid2)

#        # Node0's balance should be starting balance, plus mining_reward for another
#        # matured block, minus (starting_balance - (mining_reward - 2)), minus 5, and minus transaction fees:
#        expected = starting_balance
#        if self.options.mine_block: expected += mining_reward
#        expected += tx1["amount"] + tx1["fee"]
#        expected += tx2["amount"] + tx2["fee"]
#        assert_equal(self.nodes[0].getbalance(), expected)

#        if self.options.mine_block:
#            assert_equal(tx1["confirmations"], 1)
#            assert_equal(tx2["confirmations"], 1)
#            # Node1's total balance should be its starting balance plus both transaction amounts:
#            assert_equal(self.nodes[1].getbalance(""), starting_balance - (tx1["amount"]+tx2["amount"]))
#        else:
#            assert_equal(tx1["confirmations"], 0)
#            assert_equal(tx2["confirmations"], 0)

#        # Now give doublespend to miner:
#        self.nodes[2].sendrawtransaction(doublespend["hex"])
#        # ... mine a block...
#        self.nodes[2].generate(1)

#        # Reconnect the split network, and sync chain:
#        connect_nodes(self.nodes[1], 2)
#        self.nodes[2].generate(1)  # Mine another block to make sure we sync
#        sync_blocks(self.nodes)

#        # Re-fetch transaction info:
#        tx1 = self.nodes[0].gettransaction(txid1)
#        tx2 = self.nodes[0].gettransaction(txid2)

#        # Both transactions should be conflicted
#        assert_equal(tx1["confirmations"], -1)
#        assert_equal(tx2["confirmations"], -1)

#        # Node0's total balance should be starting balance, plus (mining_reward * 2) for
#        # two more matured blocks, minus (starting_balance - (mining_reward - 2)) for the double-spend:
#        expected = starting_balance + (mining_reward * 2) - (starting_balance - (mining_reward - 2))
#        assert_equal(self.nodes[0].getbalance(), expected)
#        assert_equal(self.nodes[0].getbalance("*"), expected)

#        # Node1's total balance should be its starting balance plus the amount of the mutated send:
#        assert_equal(self.nodes[1].getbalance(""), starting_balance + (starting_balance - (mining_reward - 2)))

if __name__ == '__main__':
    SendHeartbeatTest().main()
