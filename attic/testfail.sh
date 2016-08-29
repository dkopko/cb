#!/bin/bash

# For debugging issue where found_node_offset was being used outdatedly because
# of unnecessary copying of modifiable nodes.
#./test_measure --impl cbbst --ratios 1,0,0,1 --seed 7

# For debugging the fix to the unnecessary copying of modifiable nodes.
#./test_measure --impl cbbst --ratios 1,0,0,1 --seed 1490

# For debugging the issue with wrong cutoff_offset used for cbbst
#./test_measure --impl cbbst --ratios 1,1,1,1,1,1

# For debugging the issue where cb_map was having problems with traversal_delete
# of the deletions tree when it contained unknown keys.
#./test_measure --impl cbmap --ratios 1,1,1,1,1,1

