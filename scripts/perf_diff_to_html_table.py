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


def read_filename(filename):
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
    loaded_columnnames.append(eventdata[filename]['__NAME__'])
    loaded_filenames.append(filename)
    loadindex = loadindex + 1


def print_html():
    print '<table class="perfstats">'
    #Print header row
    print '<tr>'
    print '<th></th>' #empty corner cell
    # Print origin column header
    print '<th>', loaded_columnnames[0], '</th>'
    # Print delta column headers
    i=1
    for c in loaded_columnnames[1:]:
        if eventdata[loaded_filenames[i]]['__PATCH__']:
            print('<th>%s<br><a href="%s">patch</a></th><th>%%</th>' % (c, eventdata[loaded_filenames[i]]['__PATCH__']))
        else:
            print('<th>%s</th><th>%%</th>' % (c))
        i = i + 1
    print '</tr>'
    #Print event rows
    for e in sorted(eventnames):
        if e == '__NAME__' or e == '__PATCH__':
            continue
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
                print('<td class="perfstatincr">%0.1e</td><td class="perfstatincr">%0.1f%%</td>' % (new_val, pct))
            else:
                print('<td class="perfstatdecr">%0.1e</td><td class="perfstatdecr">%0.1f%%</td>' % (new_val, pct))
        print '</tr>'
    print '</table>'


if len(sys.argv) < 2:
    print 'Usage: ', sys.argv[0], ' <name> <filename> [<name> <filename>...]'
    print 'Where these files have been generated have been generated with \'perf stat -x \; ... \''
    exit(1)

filenames = sys.argv[1::]

for i in range(len(filenames)):
    f = filenames[i]
    read_filename(f)

print_html()

