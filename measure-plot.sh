#!/bin/bash

set -o nounset

SCRIPTNAME="$(basename "$0")"
SCRIPTROOT="$(cd "$(dirname "$0")"; pwd)"
OUTROOT="${SCRIPTROOT}"/testruns
RUNID="$(date +'%Y%m%d-%H%M%S')"
OUT_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.out"
DIFF_FILE="${OUTROOT}/${SCRIPTNAME}.${RUNID}.diff"

STDMAP_OUT_FILE="${SCRIPTROOT}/stdmap.out"
CBBST_OUT_FILE="${SCRIPTROOT}/cbbst.out"
CBMAP_OUT_FILE="${SCRIPTROOT}/cbmap.out"

mkdir -p "${OUTROOT}"

{
    git rev-parse HEAD
    echo "BEGIN DIFF"
    git diff
    echo "END DIFF"
} >"${DIFF_FILE}"


"${SCRIPTROOT}"/test_measure --ring-size=134217728 >"${OUT_FILE}" 2>&1
RET=$?

MODE="REMOVE"

grep "HIST" "${OUT_FILE}" |grep "${MODE}" |grep "stdmap" >"${STDMAP_OUT_FILE}"
grep "HIST" "${OUT_FILE}" |grep "${MODE}" |grep "cbbst" >"${CBBST_OUT_FILE}"
grep "HIST" "${OUT_FILE}" |grep "${MODE}" |grep "cbmap" >"${CBMAP_OUT_FILE}"

if [[ "${RET}" != "0" ]]
then
    echo "Failure, remove outfiles? (y/n)"
    read ANS
    if [[ "${ANS}" == "y" ]]
    then
        rm -fv "${OUT_FILE}"
        rm -fv "${DIFF_FILE}"
        rm -fv "${STDMAP_OUT_FILE}"
        rm -fv "${CBBST_OUT_FILE}"
        rm -fv "${CBMAP_OUT_FILE}"
        echo "Removed outfiles"
        exit 0
    fi
fi

"${SCRIPTROOT}/plot_measure.py" "${OUT_FILE}"
