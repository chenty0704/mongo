#!/usr/bin/env python
#
# Public Domain 2014-2015 MongoDB, Inc.
# Public Domain 2008-2014 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# test_bug016.py
#   Test that compact reduces the file size.
#

import os, run
import wiredtiger, wttest
from wiredtiger import stat
from wtscenario import multiply_scenarios, number_scenarios

# Test basic compression
class test_bug016(wttest.WiredTigerTestCase):

    types = [
        ('file', dict(uri='file:test_bug016')),
    ]
    scenarios = number_scenarios(multiply_scenarios('.', types))

    #
    # We want about 22K records that total about 130Mb.  That is an average
    # of 6196 bytes per record.  Half the records should be smaller, about
    # 2700 bytes (about 30Mb) and the other half should be larger, 9666 bytes
    # per record (about 100Mb).
    #
    # Test flow is as follows.
    #
    # 1. Populate a single table with the data, alternating record size.
    # 2. Checkpoint and get stats on the table to confirm the size.
    # 3. Delete the half of the records with the larger record size.
    # 4. Get stats on table.
    # 5. Call compact.
    # 6. Get stats on compacted table.
    #
    nrecords = 22000
    bigvalue = "abcdefghi" * 1074          # 9*1074 == 9666
    smallvalue = "ihgfedcba" * 303         # 9*303 == 2727

    fullsize = nrecords / 2 * len(bigvalue) + nrecords / 2 * len(smallvalue)

    def setUpConnectionOpen(self, dir):
        self.home = dir
        conn_params = \
            ',create,error_prefix="%s: ",' % self.shortid() + \
            'statistics=(fast)'
        try:
            conn = wiredtiger.wiredtiger_open(dir, conn_params)
        except wiredtiger.WiredTigerError as e:
            print "Failed conn at '%s' with config '%s'" % (dir, conn_params)
        return conn

    # Create a table, add keys with both big and small values.
    def test_bug016(self):
        params = 'key_format=i,value_format=S'

        self.session.create(self.uri, params)
        # Populate the table, alternating value sizes.
        c = self.session.open_cursor(self.uri, None)
        for i in range(self.nrecords):
            if i % 2 == 0:
                c[i] = str(i) + self.bigvalue
            else:
                c[i] = str(i) + self.smallvalue
        c.close()
        self.session.checkpoint()
        cstat = self.session.open_cursor(
            'statistics:' + self.uri, None, 'statistics=(size)')
        sz = cstat[stat.dsrc.block_size][2]
        cstat.close()
        self.pr('After populate ' + str(sz))
        self.assertGreater(sz, self.fullsize)

        # Remove the half of the records that are big.
        c = self.session.open_cursor(self.uri, None)
        count = 0
        for i in range(self.nrecords):
            if i % 2 == 0:
                count += 1
                c.set_key(i)
                c.remove()
        c.close()
        self.pr('Removed total ' + str(count))
        self.session.checkpoint()
        cstat = self.session.open_cursor(
            'statistics:' + self.uri, None, 'statistics=(size)')
        szm = cstat[stat.dsrc.block_size][2]
        cstat.close()
        self.pr('After remove ' + str(sz))

        # Call compact.
        self.session.compact(self.uri, None)
        cstat = self.session.open_cursor(
            'statistics:' + self.uri, None, 'statistics=(size)')
        sz = cstat[stat.dsrc.block_size][2]
        cstat.close()

        # After compact, the file size should be less than half the full size.
        self.assertLess(sz, self.fullsize / 2)
        self.pr('After compact ' + str(sz))

if __name__ == '__main__':
    wttest.run()
