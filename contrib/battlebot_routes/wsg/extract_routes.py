#!/usr/bin/env python3
"""
Snapshot of the Warsong Gulch route map of the battle bots.

Unlike Arathi Basin (see ../ab), the WSG routes are written by hand in src/game/PlayerBots/BattleBotWaypoints.cpp,
which is the source of truth. This script reads that file and writes wsg_routes.json, so the map survives as plain data
(it can be plotted, reviewed or diffed without compiling anything).

Usage:
  python3 contrib/battlebot_routes/wsg/extract_routes.py          # rewrites wsg_routes.json
  python3 contrib/battlebot_routes/wsg/extract_routes.py --check  # exits 1 if the JSON is out of date

JSON layout:
  routes[]: name, noReverse (can only be walked from first to last point), comment (lines above the definition),
            points[]: [x, y, z, callback]  (callback is the C++ function called on arrival, or "")
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CPP = os.path.join(HERE, '..', '..', '..', 'src', 'game', 'PlayerBots', 'BattleBotWaypoints.cpp')
OUT = os.path.join(HERE, 'wsg_routes.json')

src = open(CPP, encoding='utf8').read()


def block(start_pattern):
    m = re.search(start_pattern, src)
    if not m:
        sys.exit('not found: ' + start_pattern)
    end = src.index('};', m.end())
    return src[m.end():end]


listed = re.findall(r'&(vPath_WSG_\w+)', block(r'std::vector<BattleBotPath\*> const vPaths_WS =\s*\{'))
no_rev = set(re.findall(r'&(vPath_WSG_\w+)', block(r'std::vector<BattleBotPath\*> const vPaths_NoReverseAllowed =\s*\{')))

routes = []
for name in listed:
    m = re.search(r'((?:^//[^\n]*\n)*)BattleBotPath %s =\s*\{(.*?)\n\};' % re.escape(name), src, re.S | re.M)
    if not m:
        sys.exit('definition of %s not found' % name)
    comment = ' '.join(l[2:].strip() for l in m.group(1).splitlines())
    pts = []
    for x, y, z, fn in re.findall(r'\{\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(nullptr|&\w+)\s*\}', m.group(2)):
        pts.append([float(x), float(y), float(z), '' if fn == 'nullptr' else fn[1:]])
    routes.append({'name': name[len('vPath_WSG_'):], 'noReverse': name in no_rev, 'comment': comment, 'points': pts})

text = json.dumps({'routes': routes}, indent=1) + '\n'
if '--check' in sys.argv:
    sys.exit(0 if os.path.exists(OUT) and open(OUT, encoding='utf8').read() == text else 1)
open(OUT, 'w', encoding='utf8').write(text)
print('%d routes, %d points' % (len(routes), sum(len(r['points']) for r in routes)))
