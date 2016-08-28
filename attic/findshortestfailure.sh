#!/bin/bash

set -o pipefail

bestseed=0
mincases=999999
i=0

#ulimit -c 0
while true
do
    INSERT=$((RANDOM % 10))
    REMOVE=$((RANDOM % 10))

    if [[ "${INSERT}" == "0" ]]
    then
        continue
    fi

    SEED=${RANDOM}
    CMD="./test_measure --event-count 1000 --impl cbbst --ratios ${INSERT},0,0,${REMOVE} --seed ${SEED}"
    echo "Command: ${CMD}"

    cases=$(time ${CMD} 2>&1 |grep case |wc -l)
    ret=$?

    echo "cases: ${cases}, ret: ${ret}"
    echo

    if [[ "${ret}" != "0" ]]
    then
        echo "Command failed!"
        if (( ${cases} < ${mincases} ))
        then
            mincases=${cases}
            bestseed=${i}
            echo "Best command failure: \"${CMD}\""
        fi
    fi

    i=$((i + 1))
done

