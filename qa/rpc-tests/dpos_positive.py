#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test dPoS consensus work
#

import os
import sys
import time
from dpos_base import dPoS_BaseTest
from test_framework.util import \
    assert_equal, \
    assert_greater_than

class dPoS_PositiveTest(dPoS_BaseTest):
    def run_test(self):
        super(dPoS_PositiveTest, self).run_test()

        to_address = self.nodes[3].getnewaddress()
        for i in range(2):
            is_dPoS = i == 1
            rawtx = self.create_transaction(i, to_address, 10.0, is_dPoS)
            sigtx = self.nodes[i].signrawtransaction(rawtx)
            self.nodes[i].sendrawtransaction(sigtx["hex"])
            print(rawtx, sigtx)

#        preblock_hashes = self.check_progenitor_blocks(predpos_hashes)
#        pbvote_hashes = self.check_progenitor_votes(preblock_hashes)
        self.stop_nodes()

    def check_progenitor_blocks(self, predpos_hashes):
        preblock_hashes = self.mine_blocks(False, self.num_nodes)
        assert_equal(len(predpos_hashes), self.num_nodes)
        assert_equal(len(preblock_hashes), len(predpos_hashes) / 2)
        time.sleep(2)

        for node in self.nodes:
            pblocks = node.listprogenitorblocks()
            assert_equal(node.getblockcount(), self.num_nodes)
            assert_equal(len(pblocks), len(preblock_hashes))
            for idx, pblock in enumerate(pblocks):
                assert_equal(pblock["hash"], preblock_hashes[idx])

        return preblock_hashes

    def check_progenitor_votes(self, preblock_hashes):
        pbvote_hashes = []
        for node in self.nodes:
            pbvotes = node.listprogenitorvotes()
            assert_equal(node.getblockcount(), self.num_nodes)
            assert_equal(len(pbvotes), len(preblock_hashes) / 2)
            for pbvote in pbvotes:
                pbvote_hashes.append(pbvote["hash"])
#        self.nodes[0].generate(1)
#        time.sleep(2)
#        for node in self.nodes:
#            assert_equal(node.getblockcount(), self.num_nodes + 1)
        return pbvote_hashes

if __name__ == '__main__':
    dPoS_PositiveTest().main()
