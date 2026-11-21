#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check that a capture node is routed from the requested camera in an enabled media graph."""

import re
import sys


def has_route(topology, sensor, capture):
    entities = {}
    nodes = {}
    edges = set()
    current = None
    for line in topology.splitlines():
        match = re.match(r'^- entity \d+: (.+) \(\d+ pad(?:s)?, \d+ link(?:s)?\)$', line)
        if match:
            current = match[1]
            entities[current] = set()
        elif current is not None:
            node = re.match(r'^\s+device node name (/dev/\S+)', line)
            if node:
                nodes[node[1]] = current
            elif re.match(r'^\s+type .*\bsubtype Sensor\b', line):
                entities[current].add('sensor')
            link = re.match(r'^\s+-> "([^"]+)":\d+ \[([^]]*)\]', line)
            if link and 'ENABLED' in [flag.strip() for flag in link[2].split(',')]:
                edges.add((current, link[1]))

    pending = [nodes[capture]] if capture in nodes else []
    seen = set()
    while pending:
        entity = pending.pop()
        if entity in seen:
            continue
        seen.add(entity)
        if entity.split()[0].lower() == sensor and 'sensor' in entities.get(entity, ()):
            return True
        pending.extend(source for source, target in edges if target == entity)
    return False


if __name__ == '__main__':
    raise SystemExit(0 if has_route(sys.stdin.read(), sys.argv[1], sys.argv[2]) else 1)
