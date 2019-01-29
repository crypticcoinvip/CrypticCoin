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
            assert_equal(int(node.read_heartbeat_messages()), 0)

        # Check that all nodes recieved heartbeat
        current_ts = int(time.time())
        self.nodes[0].send_heartbeat_message(current_ts)
        time.sleep(1)
        for n in range(self.num_nodes):
            print(n)
            assert_equal(int(self.nodes[n].read_heartbeat_messages()), current_ts)

if __name__ == '__main__':
    SendHeartbeatTest().main()
