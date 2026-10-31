#!/usr/bin/env python3
"""
Regenerates the Arathi Basin route graph of the battle bots from ab_graph.json.

ab_graph.json is the source of truth of the map: hubs (id, name, x/y/z) and legs (id, noRev, points).
The first and last point of every leg must be the exact position of a hub. 'noRev' legs can only be walked
from their first to their last point.

Rewrites the regions between the "BEGIN GENERATED" / "END GENERATED" markers of
  src/game/PlayerBots/BattleBotABGraph.h
  src/game/PlayerBots/BattleBotABGraph.cpp
and prints the length of the routes between the bases and the flags.

Usage: python3 contrib/battlebot_routes/ab/gen_graph.py
"""
import heapq
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', '..', '..', 'src', 'game', 'PlayerBots')

# BG_AB_Nodes values of the flags: stables, blacksmith, farm, lumber mill, gold mine.
FLAG_OF_HUB = {'S': 0, 'B': 1, 'F': 2, 'L': 3, 'M': 4}

d = json.load(open(os.path.join(HERE, 'ab_graph.json'), encoding='utf8'))
hubs = d['hubs']
routes = d['routes']


def hub_id(p):
    return next(h['id'] for h in hubs if math.dist(p, h['p']) < 0.01)


def f(v):
    return ('%.6f' % v).rstrip('0').rstrip('.') + 'f'


legs = []
for r in routes:
    a, b = hub_id(r['pts'][0]), hub_id(r['pts'][-1])
    legs.append(('vPathAB_%s_%s' % (a, b), a, b, r['noRev'], r['pts']))
names = [n for n, *_ in legs]
if len(set(names)) != len(names):
    sys.exit('two legs link the same pair of hubs in the same direction')

data = []
for name, a, b, no_rev, pts in legs:
    data.append('static BattleBotPath %s =\n{\n' % name)
    for p in pts:
        data.append('    { %s, %s, %s, nullptr },\n' % (f(p[0]), f(p[1]), f(p[2])))
    data.append('};\n\n')
data.append('static Hub const aHubs[HUB_COUNT] =\n{\n')
for h in hubs:
    data.append('    { "%s", %s, %s, %s, %s }, // %s\n' % (h['id'], f(h['p'][0]), f(h['p'][1]), f(h['p'][2]), FLAG_OF_HUB.get(h['id'], 255), h['n']))
data.append('};\n\n')
data.append('static Edge const aEdges[] =\n{\n')
for name, a, b, no_rev, _ in legs:
    data.append('    { HUB_%s, HUB_%s, &%s, %s },\n' % (a, b, name, 'true' if no_rev else 'false'))
data.append('};\n')

vpaths = ['std::vector<BattleBotPath*> const vPaths_AB =\n{\n']
for name in names:
    vpaths.append('    &ABGraph::%s,\n' % name)
vpaths.append('};\n')

enum = []
for i, h in enumerate(hubs):
    enum.append('        HUB_%s%s,\n' % (h['id'], ' = 0' if i == 0 else ''))
enum.append('        HUB_COUNT\n')


def splice(path, what, text):
    s = open(path, encoding='utf8').read()
    pattern = re.compile(r'(// BEGIN GENERATED: ab_graph %s [^\n]*\n)(.*?)([ \t]*// END GENERATED: ab_graph %s\n)' % (what, what), re.S)
    if not pattern.search(s):
        sys.exit('markers for %s not found in %s' % (what, path))
    s = pattern.sub(lambda m: m.group(1) + text + m.group(3), s, count=1)
    open(path, 'w', encoding='utf8').write(s)


splice(os.path.join(SRC, 'BattleBotABGraph.h'), 'hubs', ''.join(enum))
splice(os.path.join(SRC, 'BattleBotABGraph.cpp'), 'data', ''.join(data))
splice(os.path.join(SRC, 'BattleBotABGraph.cpp'), 'legs', ''.join(vpaths))

# Checks and distances.
length = lambda p: sum(math.dist(p[i], p[i + 1]) for i in range(len(p) - 1))
arcs = {}
for name, a, b, no_rev, pts in legs:
    arcs.setdefault(a, []).append((b, length(pts)))
    if not no_rev:
        arcs.setdefault(b, []).append((a, length(pts)))


def dijkstra(src):
    dist = {src: 0}
    q = [(0, src)]
    while q:
        c, u = heapq.heappop(q)
        if c > dist[u]:
            continue
        for v, w in arcs.get(u, []):
            if c + w < dist.get(v, 1e18):
                dist[v] = c + w
                heapq.heappush(q, (c + w, v))
    return dist


ids = [h['id'] for h in hubs]
bad = False
for h in ids:
    miss = [x for x in FLAG_OF_HUB if x not in dijkstra(h)]
    if miss:
        bad = True
        print('WARNING: %s cannot reach %s' % (h, miss))
print('%d hubs, %d legs' % (len(hubs), len(legs)))
for start in ('AS', 'HS'):
    dist = dijkstra(start)
    print('from %s: %s' % (start, ', '.join('%s %d yd' % (k, dist[k]) for k in FLAG_OF_HUB if k in dist)))
sys.exit(1 if bad else 0)
