#!/usr/bin/env python3

# Precompute gamma correction lookup tables
# Copyright (C) 2024 Calvin Owens <calvin@wbinvd.org>
#
# Permission to use, copy, modify, and/or distribute this software for
# any purpose with or without fee is hereby granted, provided that the
# above copyright notice and this permission notice appear in all
# copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
# WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
# AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
# DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
# PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
# TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

GAMMAVALS = [1.0, 0.125, 0.25, 0.5, 0.75, 1.25, 1.5, 1.75, 2.0, 4.0]
VALSTRINGS = ['"%1.2f"' % x for x in GAMMAVALS]
NR = len(GAMMAVALS)

print("""\
#pragma once

#include <stdint.h>

/*
 * 8-bit gamma correction lookup tables precomputed by util/gamma.py
 * To change preset values, edit the GAMMAVALS array at the top of that script
 */
""")
print("static const char *gammavals[" + str(NR) + "] = {")
print("\t" + ','.join(VALSTRINGS))
print("};")
print("")
print("static const int nr_gammavals = sizeof(gammavals) / sizeof(*gammavals);")
print("")
print("static const uint8_t gammalookup[" + str(NR) + "][256] = {")

for GVAL in GAMMAVALS:
	print("{")

	for i in range(256):
		print("% 4d," % round(pow(i / 255, 1 / GVAL) * 255, 0), end='')
		if i % 16 == 15:
			print("")

	print("},")

print("};")
