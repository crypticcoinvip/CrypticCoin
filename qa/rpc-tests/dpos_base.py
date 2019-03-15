#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Base class for dPoS integration tests
#
import sys
import time
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import \
    assert_equal, \
    assert_true, \
    assert_false, \
    assert_greater_than, \
    start_nodes, \
    stop_nodes, \
    sync_blocks, \
    sync_mempools, \
    initialize_chain, \
    connect_nodes_bi, \
    wait_bitcoinds


INITIAL_BLOCK_COUNT = 200

class dPoS_BaseTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_option("--node-garaph-layout",
                          dest="node_garaph_layout",
                          default="1",
                          help="Test dPoS work with different connected nodes")
    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir)

    def setup_chain(self):
        print("Initializing dPoS Test directory "+self.options.tmpdir)
        self.num_nodes = 10
        self.operators = []
        self.is_network_split = False
        initialize_chain(self.options.tmpdir, self.num_nodes)
        sys.stdout.flush()

    def setup_network(self):
        self.nodes = self.setup_nodes()
        assert_equal(len(self.nodes), self.num_nodes)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 4)
        connect_nodes_bi(self.nodes, 4, 5)
        connect_nodes_bi(self.nodes, 5, 6)
        connect_nodes_bi(self.nodes, 6, 7)
        connect_nodes_bi(self.nodes, 7, 8)
        connect_nodes_bi(self.nodes, 8, 9)
        self.sync_all()
        for n in range(self.num_nodes):
            self.operators.append(self.nodes[n].getnewaddress())
        self.stop_nodes()

    def start_nodes(self, args = []):
        assert_equal(len(self.nodes), 0)

        if len(args) == 0:
            args = [[]] * self.num_nodes

        for i in range(len(args)):
            args[i] = args[i][:]
            args[i] += [
                '-nuparams=5ba81b19:201', # Overwinter
                '-nuparams=76b809bb:201'  # Suppling
            ]
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, args);

    def stop_nodes(self):
        stop_nodes(self.nodes)
        wait_bitcoinds()

    def sync_nodes(self, first, last):
        sync_blocks(self.nodes[first:last])
        sync_mempools(self.nodes[first:last])

    def check_nodes_block_count(self, blockCount):
        [assert_equal(node.getblockcount(), blockCount) for node in self.nodes]

    def wait_block(self, node, height, timeout=12):
        for i in range(timeout):
            time.sleep(1)
            if node.getblockcount() == height:
               time.sleep(1)
               self.sync_all()
               break
        self.check_nodes_block_count(height)

    def connect_nodes(self):
        if self.options.node_garaph_layout == "1":
            print("                      *-*-*")
            print("Nodes graph layout *<-*-*-*")
            print("                      *-*-*")
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 0, 2)
            connect_nodes_bi(self.nodes, 0, 3)
            connect_nodes_bi(self.nodes, 1, 4)
            connect_nodes_bi(self.nodes, 2, 5)
            connect_nodes_bi(self.nodes, 3, 6)
            connect_nodes_bi(self.nodes, 4, 7)
            connect_nodes_bi(self.nodes, 5, 8)
            connect_nodes_bi(self.nodes, 6, 9)
        elif self.options.node_garaph_layout == "2":
            print("                          *")
            print("                        *<")
            print("                       /  *")
            print("Nodes graph layout *-*<     *")
            print("                       \  *<")
            print("                        *<  *")
            print("                          *")
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 1, 3)
            connect_nodes_bi(self.nodes, 2, 4)
            connect_nodes_bi(self.nodes, 2, 5)
            connect_nodes_bi(self.nodes, 3, 6)
            connect_nodes_bi(self.nodes, 3, 7)
            connect_nodes_bi(self.nodes, 6, 8)
            connect_nodes_bi(self.nodes, 6, 9)
        elif self.options.node_garaph_layout == "3":
            print("                     *-*-*-*")
            print("Check graph layout *<       >*")
            print("                     *-*-*-*")
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 4)
            connect_nodes_bi(self.nodes, 4, 5)
            connect_nodes_bi(self.nodes, 5, 6)
            connect_nodes_bi(self.nodes, 6, 7)
            connect_nodes_bi(self.nodes, 7, 8)
            connect_nodes_bi(self.nodes, 8, 9)
            connect_nodes_bi(self.nodes, 9, 0)
        else:
            raise ValueError("Invalid argument --node_garaph_layout")
        self.sync_all()

    def create_masternodes(self, indexes={0,1,2,3,4,5}):
        # Announce nodes
        rv = []
        for n in range(self.num_nodes):
            idnode = None
            if n in indexes:
                owner = self.nodes[n].getnewaddress()
                collateral = self.nodes[n].getnewaddress()
                idnode = self.nodes[n].mn_announce([], {
                    "name": "node%d" % n,
                    "ownerAuthAddress": owner,
                    "operatorAuthAddress": self.operators[n],
                    "ownerRewardAddress": owner,
                    "collateralAddress": collateral
                })
            rv.append(idnode)
        for n in range(self.num_nodes):
            if n not in indexes:
                assert_equal(rv[n], None)


        # Sending some coins for auth
        for n in range(self.num_nodes):
            self.nodes[n].sendtoaddress(self.operators[n], 25.25)
            if not n in indexes:
                self.nodes[n].generate(1)
                self.sync_all()

        self.nodes[self.num_nodes / 2].generate(1)
        self.sync_all()

        for node in self.nodes:
            for idnode in rv:
                if idnode:
                    assert_equal(node.mn_list([idnode])[0]['status'], "announced")

        self.check_nodes_block_count(INITIAL_BLOCK_COUNT + self.num_nodes - len(indexes) + 1)


        # Waiting for autofinalize
        for node in self.nodes:
            blockCount = node.getblockcount() + 1
            node.generate(1)
            self.wait_block(node, blockCount)

        self.check_nodes_block_count(INITIAL_BLOCK_COUNT + 2 * self.num_nodes - len(indexes) + 1)

        # Retrieve some funds
        z_coins = []
        for node in self.nodes:
            blockCount = node.getblockcount() + 1
            z_coins.append(node.z_getnewaddress())
            node.z_shieldcoinbase('*', z_coins[-1])
            node.generate(1)
            self.wait_block(node, blockCount)

        # Transfer funds to operators
        for n in range(self.num_nodes):
            blockCount = self.nodes[n].getblockcount() + 1
            next_idx = n + 1 if n + 1 < self.num_nodes else 0
            zdst = [{ "address": self.operators[next_idx], "amount": 33.33 }]
            ztx = self.nodes[n].z_sendmany(z_coins[n], zdst)
            time.sleep(2)
            ztx = self.nodes[n].z_getoperationstatus([ztx])
            assert_equal(ztx[0]["status"], "success")
            self.nodes[n].generate(1)
            self.wait_block(self.nodes[n], blockCount)

        # Activate mannualy
        for n in range(self.num_nodes):
            if n in indexes:
                self.nodes[n].mn_activate([])
                blockCount = self.nodes[n].getblockcount() + 1
                self.nodes[n].generate(1)
                self.wait_block(self.nodes[n], blockCount)

        self.check_nodes_block_count(INITIAL_BLOCK_COUNT + 4 * self.num_nodes + 1)

        for node in self.nodes:
            assert_equal(node.getinfo()["dpos"], True)
            for idnode in rv:
                if idnode:
                    assert_equal(node.mn_list([idnode])[0]['status'], "active")
        return rv

    def create_transaction(self, node_idx, to_address, amount, instantly):
        node = self.nodes[node_idx]
        outputs = { to_address : int((amount - amount * 0.000001) * 100000) / 100000.0 }
        rawtx = node.createrawtransaction([], outputs, 0, node.getblockcount() + 21, instantly)
        fundtx = node.fundrawtransaction(rawtx)
        sigtx = node.signrawtransaction(fundtx["hex"])
        return node.sendrawtransaction(sigtx["hex"])

    def start_masternodes(self, args=[]):
        extra_args = [["-masternode_operator="+oper] for oper in self.operators]
        for i in range(min(len(args), len(extra_args))):
            extra_args[i] = extra_args[i] + args[i]
        self.start_nodes(extra_args)

    def run_test(self):
        assert_equal(self.num_nodes, 10)
        self.start_masternodes()
        self.connect_nodes()
        assert_equal(len(self.nodes), self.num_nodes)
        for node in self.nodes:
            assert_equal(node.getblockcount(), INITIAL_BLOCK_COUNT)
            assert_equal(node.getbalance(), 100.0)

assert(__name__ != '__main__')
