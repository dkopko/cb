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
            y = x.split(' ')
            eventname = y[0] if len(y) > 0 else 'n/a'
            count     = y[1] if len(y) > 1 else 'n/a'
            #print 'eventname: ', eventname, ' count: ', count
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
    print '<table class="perfstats">'
    #Print header row
    print '<tr>'
    print '<th></th>' #empty corner cell
    print '<th>', loaded_columnnames[0], '</th>'
    for c in loaded_columnnames[1:]:
        print '<th>', c, '</th>'
        print '<th>%</th>'
    print '</tr>'
    #Print event rows
    for e in eventnames:
        print '<tr>'
        print '<td>', e, '</td>'
        # Print origin column
        origin_val = float(eventdata[loaded_filenames[0]][e])
        print('<td>%0.1e</td>' % (origin_val))
        # Print delta columns
        for f in loaded_filenames[1:]:
            new_val = float(eventdata[f][e])
            pct = (new_val / origin_val) * 100.0 if origin_val != 0 else 0
            if new_val > origin_val:
                print('<td class="perfstatincr">%0.1e</td><td class="perfstatincr">(%0.1f%%)</td>' % (new_val, pct))
            else:
                print('<td class="perfstatdecr">%0.1e</td><td class="perfstatdecr">(%0.1f%%)</td>' % (new_val, pct))
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

