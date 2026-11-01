#!/usr/bin/env python3
#
# frank-micro — BBC Micro for RP2350
#
# Copyright (c) 2026 Tim Hawes <me@timhawes.com>
# https://github.com/rh1tech/frank-micro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build mode7.ssd, a MODE 7 test disc.  Its !BOOT (*EXEC) loads a ready-made
# 1,000-byte teletext screen to &7C00, so nothing has to be typed.  Copy it to
# /micro/disk/ and mount it from the disc loader; mounting autoboots it.
#
# Rows, top to bottom:
#    0      yellow on a blue background
#    1-4    double height in all seven colours
#    5      normal height in all seven colours
#    6, 7   contiguous and separated graphics blocks
#    8, 9   every mosaic character
#    10     flashing and steady
#    11     text on red and on blue backgrounds
#    12-13  double height on a blue background
#    14-16  the character set, 32 to 126, then easily confused glyphs
#    17-18  a lone top half of double height, then a normal row
#    19-20  double, normal and double height on one row
#    21     uppercase "blast-through" inside graphics
#
# Usage: mkmode7test.py OUT.ssd
import os, sys
R,G,Y,B,M,C,W = range(129,136)
def gfx(c): return c+16
DH,NH,FL,ST,NB,BG,SEP,CON = 141,140,136,137,156,157,154,153
rows=[]
def row(*parts):
    b=bytearray()
    for p in parts:
        b += p.encode('latin1') if isinstance(p,str) else bytes([p]) if isinstance(p,int) else bytes(p)
    assert len(b)<=40,(len(b),b)
    rows.append(bytes(b.ljust(40,b' ')))
row(B,BG,Y,"MODE 7 TEST  frank-micro",NB)            # 0
row(DH,R,"Double RED ",G,"GREEN ",Y,"YELLOW")         # 1 top
row(DH,R,"Double RED ",G,"GREEN ",Y,"YELLOW")         # 2 bottom
row(DH,B,"Blue ",M,"Magenta ",C,"Cyan ",W,"White")   # 3
row(DH,B,"Blue ",M,"Magenta ",C,"Cyan ",W,"White")   # 4
row(R,"Red",G,"Green",Y,"Yell",B,"Blue",M,"Mag",C,"Cyan",W,"Wht")  # 5
row(*sum([[gfx(c),255,255,255,255] for c in (R,G,Y,B,M,C,W)],[]))  # 6 contiguous blocks
row(SEP,*sum([[gfx(c),255,255,255] for c in (R,G,Y,B,M,C,W)],[]),CON) # 7 separated
row(gfx(W),bytes(range(160,192)))                     # 8 mosaics
row(gfx(W),bytes(range(224,256)))                     # 9 mosaics
row(FL,"Flashing",ST," steady ",FL,C,"cyan flash")   # 10
row(R,BG,Y,"Yellow on red ",NB," normal ",B,BG,W,"on blue",NB) # 11
row(DH,B,BG,W,"Double on blue ",NB,G,"green")        # 12
row(DH,B,BG,W,"Double on blue ",NB,G,"green")        # 13
row(bytes(range(32,72)))                              # 14
row(bytes(range(72,112)))                             # 15
row(bytes(range(112,127)),W," 0O 1lI #_[]{}|~")      # 16
row(DH,Y,"Top half only, no partner")                # 17
row(W,"Normal row after a lone top half")            # 18
row(DH,C,"Double",NH,"normal",DH,"Double")           # 19
row(DH,C,"Double",NH,"normal",DH,"Double")           # 20
row(gfx(G),"Text in graphics: ABC abc 123")          # 21
while len(rows)<25: rows.append(b' '*40)
screen=b''.join(rows); assert len(screen)==1000
boot=b'MODE7:VDU23,1,0;0;0;0;31,0,22:OSCLI"LOAD SCREEN 7C00"\r'

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkssd import build   # tools/mkssd.py: the DFS catalogue

image = build([("!BOOT", 0, 0, boot), ("SCREEN", 0x7C00, 0x7C00, screen)],
              "MODE7TEST", 3)
open(sys.argv[1], "wb").write(image)
