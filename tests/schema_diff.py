#!/usr/bin/env python3
# Copyright 2026, the Meshy for Haiku contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compares the schema this port creates with one created by upstream Meshy's
# own storage.py, so a database written by either side stays interchangeable.
# Usage: schema_diff.py <cpp.db> <upstream.db>

import sqlite3
import sys


def schema(path):
    db = sqlite3.connect(path)
    out = {}
    tables = [
        r[0]
        for r in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"
        )
    ]
    for t in sorted(tables):
        cols = {}
        for row in db.execute("PRAGMA table_info(%s)" % t):
            # (cid, name, type, notnull, default, pk)
            cols[row[1]] = (row[2].upper(), row[3], row[4], row[5])
        out[t] = cols
    db.close()
    return out


def main():
    a = schema(sys.argv[1])
    b = schema(sys.argv[2])
    problems = []

    only_cpp = sorted(set(a) - set(b))
    only_up = sorted(set(b) - set(a))
    for t in only_up:
        problems.append("table missing in port: %s" % t)
    for t in only_cpp:
        # Extra tables are harmless for interoperability, but worth printing.
        print("note: table only in port: %s" % t)

    for t in sorted(set(a) & set(b)):
        ca, cb = a[t], b[t]
        for col in sorted(set(cb) - set(ca)):
            problems.append("%s: column missing in port: %s" % (t, col))
        for col in sorted(set(ca) - set(cb)):
            print("note: %s: column only in port: %s" % (t, col))
        for col in sorted(set(ca) & set(cb)):
            if ca[col][0] != cb[col][0]:
                problems.append(
                    "%s.%s: type %s vs upstream %s" % (t, col, ca[col][0], cb[col][0])
                )
            if ca[col][3] != cb[col][3]:
                problems.append(
                    "%s.%s: pk %s vs upstream %s" % (t, col, ca[col][3], cb[col][3])
                )

    if problems:
        print("\n".join("MISMATCH " + p for p in problems))
        return 1
    print("schema compatible with upstream")
    return 0


if __name__ == "__main__":
    sys.exit(main())
