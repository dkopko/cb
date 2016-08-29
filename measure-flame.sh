#!/bin/bash

set -o nounset

SCRIPTNAME="$(basename "$0")"
SCRIPTROOT="$(cd "$(dirname "$0")"; pwd)"
OUTROOT="${SCRIPTROOT}"/testruns
RUNID="$(date +'%Y%m%d-%H%M%S')"
OUT_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.out"
DIFF_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.diff"
FOLDED_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.folded"
STDMAP_FLAME_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.stdmap_flame.svg"
CBBST_FLAME_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.cbbst_flame.svg"
CBMAP_FLAME_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.cbmap_flame.svg"

mkdir -p "${OUTROOT}"

{
    git rev-parse HEAD
    echo "BEGIN DIFF"
    git diff
    echo "END DIFF"
} >"${DIFF_FILE}"


perf record -F 999 -a -g -- \
    "${SCRIPTROOT}"/test_measure --ring-size=134217728 >"${OUT_FILE}" 2>&1
RET=$?
grep "TEST_RESULT" "${OUT_FILE}"

function exclude_kernel()
{ 
    sed -e 's/;[^;]*_\[k\]//g'
}

perf script >out.perf
~/workspace/FlameGraph/stackcollapse-perf.pl --kernel out.perf >"${FOLDED_FILE}"
cat "${FOLDED_FILE}" |exclude_kernel |fgrep stdmap_handle_events |flamegraph.pl >"${STDMAP_FLAME_FILE}"
cat "${FOLDED_FILE}" |exclude_kernel |fgrep cbbst_handle_events |flamegraph.pl >"${CBBST_FLAME_FILE}"
cat "${FOLDED_FILE}" |exclude_kernel |fgrep cbmap_handle_events |grep -v cb_map_consolidate |flamegraph.pl >"${CBMAP_FLAME_FILE}"

grep DANDEBUG "${OUT_FILE}"

if [[ "${RET}" != "0" ]]
then
    echo "Failure, remove outfiles? (y/n)"
    read ANS
    if [[ "${ANS}" == "y" ]]
    then
        rm -fv "${OUT_FILE}"
        rm -fv "${DIFF_FILE}"
        rm -fv "${FOLDED_FILE}"
        rm -fv "${STDMAP_FLAME_FILE}"
        rm -fv "${CBBST_FLAME_FILE}"
        rm -fv "${CBMAP_FLAME_FILE}"
        echo "Removed outfiles"
        exit 0
    fi
fi

ln -sf "${OUT_FILE}" "${SCRIPTROOT}/${SCRIPTNAME}.out"
ln -sf "${DIFF_FILE}" "${SCRIPTROOT}/${SCRIPTNAME}.diff"
ln -sf "${FOLDED_FILE}" "${SCRIPTROOT}/${SCRIPTNAME}.folded"
ln -sf "${STDMAP_FLAME_FILE}" "${SCRIPTROOT}/${SCRIPTNAME}.stdmap_flame.svg"
ln -sf "${CBBST_FLAME_FILE}" "${SCRIPTROOT}/${SCRIPTNAME}.cbbst_flame.svg"
ln -sf "${CBMAP_FLAME_FILE}" "${SCRIPTROOT}/${SCRIPTNAME}.cbmap_flame.svg"

