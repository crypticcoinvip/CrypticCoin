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

_WALLET_KEYS = [
    ("cSMhqrt1wStUuEnCyV97P3P9sUXVXVpn6Jb1iQ9VR5NPHYiMskkX", "tmXWM1dEwR8ANoc8iwwQ4pKzHvApXb65LsG"),
    ("cQvcpuFVeKHBmtCDHu1mXx8tU5b4HHw52FcUg62ZvALvaT6yrst9", "tmXbhS7QTA98nQhd3NMf7xe7ETLXiU4Dp41"),
    ("cPzSk8LGtysk8J5UEZDFdh7gVs8xGXd7PvSfgBoJtPKH4vnzH5kL", "tmYgqY2N9ykAqv9YqRsoPi16iojB9VuxBvY"),
    ("cQCtVNgf4J3CTTMtLqeXeeoFiZVCsSjv8gG7TRRD1xL69owZ1j9Z", "tmYDASd8j9bBLEVmiaU8tArTYmSVA9DJjXW")
]

_HEARTBEAT_MESSAGES = [
    (
        1548909732001,
        "4235f89122266fa41adb85220be7557db7b3f6b08110755eeabe48c979806c03",
        "204fbc3595077481d7ce513611c03ebd6b044fc036b91b16e3cf2b2aaf8b31f8296ad242c77af375d0730cb456b52d8f09b9412815926247bf423970bcaf328af3"
    ),
    (
        1548909732002,
        "ee56971fa6a632759bd1d68b2ce4dc6d4b8267ffd02c1d5b72cf5e7892b0044c",
        "1fa673cb7ea7e329d662acf28d9102f7408f9bb65c286851f96844a2d3da56e7f058085bec1942af879a0e67d8ed5fb4be604b4e8fa63130f93b96df40bead95aa"
    ),
    (
        1548909732003,
        "623188061bd9302235a1b85717fbca6057d4760b83acc54fc61d92eb37eea499",
        "1fe47dec57b46f7dfd1a66558484f8ecb3deff082d5f306a3cd0f3e7c2e38295672544e489f85243b0e88c8624b4936168be7b0fa2dd8f24dcb3801575c966ed4f"
    ),
    (
        1548909732004,
        "6ddd055c58239d67d14bbd266bb7a8692600d2aa6b0423b82e612b826ba71268",
        "20382862d7bc9e8af98c709781ecf716693ca27117cd74615c6a48f76e12c2e26a08b76e6f67d359d8964b954a710b8623ee92221d311d2917cc26ea20eb8500af"
    )
]


class SendHeartbeatTest(BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing SendHeartbeatTest directory "+self.options.tmpdir)
        self.num_nodes = len(_WALLET_KEYS)
        self.is_network_split=False
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)

    def setup_network(self):
        super(SendHeartbeatTest, self).setup_network(False)
        self.stop_nodes()

    def run_test(self):
        assert_equal(len(self.nodes), 0)
        assert_equal(len(_WALLET_KEYS), self.num_nodes)
        assert_equal(len(_HEARTBEAT_MESSAGES), self.num_nodes)

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
        self.check_heartbeat_sync()

        print("                        *")
        print("Check graph layout *-*<")
        print("                        *")

        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 1, 3)
        self.check_heartbeat_sync()

        print("                      *")
        print("Check graph layout *<   >*")
        print("                      *")

        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        self.check_heartbeat_sync()

    def start_nodes(self, args = []):
        if len(args) == 0:
            args = [[]] * self.num_nodes
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);
        for n in range(self.num_nodes):
            wallet_key = _WALLET_KEYS[n]
            assert_equal(self.nodes[n].importprivkey(wallet_key[0]), wallet_key[1])

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def check_heartbeat_messages(self, sender_idx, before_messages = []):
        hb_msg = _HEARTBEAT_MESSAGES[sender_idx]
        assert_equal(len(self.nodes), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.getblockcount(), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.mn_readheartbeats(), before_messages)

        # Check that all nodes recieved heartbeat
        msg = self.nodes[sender_idx].mn_sendheartbeat(_WALLET_KEYS[sender_idx][1], hb_msg[0])
        assert_equal(msg["timestamp"], hb_msg[0])
        assert_equal(msg["signature"], hb_msg[2])
        assert_equal(msg["hash"], hb_msg[1])
        time.sleep(1)
        rv = before_messages + [msg]
        for n in range(self.num_nodes):
            assert_equal(self.nodes[n].mn_readheartbeats(), rv)
        return rv

    def check_heartbeat_sync(self):
        before_messages = []
        for n in range(self.num_nodes):
            before_messages = self.check_heartbeat_messages(n, before_messages)
        self.stop_nodes()



if __name__ == '__main__':
    SendHeartbeatTest().main()
