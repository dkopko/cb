#!/usr/bin/env python
import math
import random
import numpy
import sys
from matplotlib import pyplot

"""
TODO
* Move key elsewhere
* Output a picture file
* Boxplot option
* Array generator to replace .extend() call?
"""

script, filename = sys.argv

known_impls = { }
known_ops = { }
impl_measures = { }

with open(filename,'r') as f:
    for x in f:
        x = x.rstrip()
        if not x:
            continue
        y = x.split(' ')
        if y[0] != 'HIST':
            continue
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
rows=len(known_ops)
cols=1
fig, axes = pyplot.subplots(nrows=rows, ncols=cols, figsize=(8.5, 11), sharex=True, sharey=True)


maxs = []
mins = []
for op in known_ops.keys():
    for impl in known_impls.keys():
        x = impl_measures[impl][op]
        maxs.append(max(x))
        mins.append(min(x))
bins = numpy.logspace(0.1, numpy.log10(max(maxs)), 500)

i = 0
for op in sorted(known_ops.keys()):
    ax = axes[i]
    ax.set_title(op)
    ax.set_xscale("log")
    ax.set_yscale("symlog", linthreshy=10.0)
    ax.grid()
    """
    Histogram
    """
    for impl in known_impls.keys():
        ax.hist(impl_measures[impl][op], bins=bins, label=impl, histtype='step')
    """
    Boxplot
    ax.boxplot([impl_measures[im][op] for im in known_impls.keys()], notch=True, vert=False, whis=[0, 99], labels=known_impls.keys())
    ax.set_xlim(left=1)
    """
    i=i+1

pyplot.tight_layout()
pyplot.legend()
pyplot.show()
