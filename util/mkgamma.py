#!/usr/bin/python3 -u

## Precompute gamma correction lookup tables
## Copyright (C) 2023 Calvin Owens <jcalvinowens@gmail.com>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <https://www.gnu.org/licenses/>.

GAMMAVALS = [1.0, 0.125, 0.25, 0.5, 0.75, 1.25, 1.5, 1.75, 2.0, 4.0]

NR = len(GAMMAVALS)
ST = ['"%1.2f"' % x for x in GAMMAVALS]
print("#pragma once")
print("")
print("#include <stdint.h>")
print("")
print("/*")
print(" * 8-bit gamma correction lookup tables precomputed by util/mkgamma.py")
print(" * To change preset values, edit the GAMMAVALS array at the top of that script")
print(" */")
print("")
print("static const char *gammavals[" + str(NR) + "] = {" + ','.join(ST) + "};")
print("static const int nr_gamma_vals = sizeof(gammavals) / sizeof(gammavals[0]);")
print("")
print("static const uint8_t gammalookup[" + str(NR) + "][256] = {")
for GVAL in GAMMAVALS:
	print("{" + ','.join(
	[str(int(round(pow(i / 255, 1 / GVAL) * 255, 0))) for i in range(256)]
	) + "},")
print("};")
