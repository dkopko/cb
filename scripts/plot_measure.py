#!/usr/bin/env python

# Copyright 2016 Daniel Kopko.

import math
import random
import numpy
import sys
from matplotlib import pyplot

"""
TODO
* Move legend elsewhere
* Array generator to replace .extend() call?
"""

script, filename = sys.argv

known_impls = { }
known_ops = { }
impl_measures = { }

with open(filename,'r') as f:
    for x in f:
        # Skip empty lines:
        x = x.rstrip()
        if not x:
            continue
        # Skip lines not beginning with 'HIST':
        y = x.split(' ')
        if y[0] != 'HIST':
            continue
        # Parse line format: 'HIST' <impl> <op> <bucket> <count>:
        impl = y[1]
        op = y[2]
        bucket = numpy.int(y[3])
        count = numpy.int(y[4])
        arr = [bucket for i in xrange(count)]
        """ print 'bucket: ', bucket, 'count: ', count, 'arr: ', arr """
        if not impl in impl_measures:
            impl_measures[impl] = {}
            known_impls[impl] = 1
        if not op in impl_measures[impl]:
            impl_measures[impl][op] = []
            known_ops[op] = 1
        impl_measures[impl][op].extend(arr)


"""
Layout.
"""
rows = len(known_ops)
cols = 1
fig, axes = pyplot.subplots(nrows=rows, ncols=cols, sharex=True, sharey=True)


"""
Determine the min and max across all operations, to be used for unifying all
graphing.
"""
maxs = []
mins = []
for op in known_ops.keys():
    for impl in known_impls.keys():
        x = impl_measures[impl][op]
        maxs.append(max(x))
        mins.append(min(x))
bins = numpy.logspace(0.1, numpy.log10(max(maxs)), 500)


"""
For each operation type, graph each implementation as a histogram and overlay
a boxplot.  We use a symlog on the y axis because some bins will have a zero
count.
"""
i = 0
for op in sorted(known_ops.keys()):
    ax = axes[i]
    ax.set_title(op)
    ax.set_xscale("log")
    ax.set_yscale("symlog", linthreshy=10.0)
    ax.grid()
    # Histogram
    for impl in known_impls.keys():
        ax.hist(impl_measures[impl][op], bins=bins, label=impl, histtype='step')
    # Boxplot
    ax2 = axes[i].twinx();
    ax2.boxplot([impl_measures[im][op] for im in known_impls.keys()], notch=True, vert=False, whis=[0, 99], labels=known_impls.keys(), sym='')
    ax2.set_xlim(left=1)
    i=i+1

pyplot.legend(loc='best')
pyplot.tight_layout()
pyplot.savefig("figure.png")
pyplot.savefig("figure.svg")

#mng = pyplot.get_current_fig_manager()
#mng.resize(*mng.window.maxsize())
#pyplot.show()
