#!/bin/bash

set -o pipefail

bestseed=0
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
    echo
    echo
    echo "Command: ${CMD}"

    time ${CMD} >/dev/null 2>&1
    ret=$?
    if [[ "${ret}" == "0" ]]
    then
        echo "Command success!"
    else
        echo "Command failed! (ret: ${ret})"
    fi

    i=$((i + 1))
done

