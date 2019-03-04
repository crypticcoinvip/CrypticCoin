#!/usr/bin/env python2
# Copyright (c) 2019 The Crypticcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Test dPoS p2p messages
#

import os
import sys
from time import sleep
from dpos_base import dPoS_BaseTest
from test_framework.util import \
    assert_equal, \
    assert_greater_than

class dPoS_p2pMessagesTest(dPoS_BaseTest):
    def run_test(self):
        super(dPoS_p2pMessagesTest, self).run_test()


if __name__ == '__main__':
    dPoS_p2pMessagesTest().main()
