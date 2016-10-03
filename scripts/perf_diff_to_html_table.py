#!/usr/bin/env python

# Copyright 2016 Daniel Kopko.

import math
import sys

loadindex = 0
loaded_columnnames = []
loaded_filenames = []
known_eventnames = { }
eventnames = []
eventdata = { }


def read_filename(columnname, filename):
    global loadindex
    with open(filename, 'r') as f:
        for x in f:
            # Skip empty lines:
            x = x.rstrip()
            if not x:
                continue
            # Skip lines beginning with '#':
            if x[0] == '#':
                continue
            # Split the row (seems to differ in format from 'man perf-stat')
            y = x.split(';')
            count       = y[0] if len(y) > 0 else 'n/a'
            eventname   = y[2] if len(y) > 2 else 'n/a'
            measurepct  = y[4] if len(y) > 4 else 'n/a'
            metric_val  = y[5] if len(y) > 5 else 'n/a'
            metric_unit = y[6] if len(y) > 6 else 'n/a'
            #print 'eventname: ', eventname, ' count: ', count, ' ', metric_val, ' ', metric_unit, ' (', measurepct, '%)'
            if not eventname in known_eventnames:
                known_eventnames[eventname] = 1
                eventnames.append(eventname)
            if not filename in eventdata:
                eventdata[filename] = { }
            eventdata[filename][eventname] = count
    loaded_columnnames.append(columnname)
    loaded_filenames.append(filename)
    loadindex = loadindex + 1


def print_html():
    print '<table>'
    #Print header row
    print '<tr>'
    print '<th></th>' #empty corner cell
    for c in loaded_columnnames:
        print '<th>', c, '</th>'
    print '</tr>'
    #Print event rows
    for e in eventnames:
        print '<tr>'
        print '<td>', e, '</td>'
        # Print origin column
        origin_val = float(eventdata[loaded_filenames[0]][e])
        print('<td>%0.1f</td>' % (origin_val))
        # Print delta columns
        for f in loaded_filenames[1:]:
            new_val = float(eventdata[f][e])
            delta = new_val - origin_val
            delta_pct = (delta / origin_val) * 100.0 if origin_val != 0 else 0
            if delta > 0:
                print('<td>%0.1f (+%0.1f%%)</td>' % (new_val, delta_pct))
            else:
                print('<td>%0.1f (%0.1f%%)</td>' % (new_val, delta_pct))
        print '</tr>'
    print '</table>'


if len(sys.argv) < 2:
    print 'Usage: ', sys.argv[0], ' <name> <filename> [<name> <filename>...]'
    print 'Where these files have been generated have been generated with \'perf stat -x \; ... \''
    exit(1)

columnnames = sys.argv[1::2]
filenames = sys.argv[2::2]

for i in range(len(filenames)):
    c = columnnames[i]
    f = filenames[i]
    read_filename(c, f)

print_html()

