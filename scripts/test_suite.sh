#!/usr/bin/env bash
# Copyright 2016 Daniel Kopko.

set -o nounset

PROJECT_ROOT="$(cd "$(dirname "$0")/.." ; pwd)"
SCRIPTS_ROOT="${PROJECT_ROOT}/scripts"

#Overrideable settings
BUILD_ROOT="${BUILD_ROOT:-${PROJECT_ROOT}/BUILD}"
TESTRUNS_ROOT="${TESTRUNS_ROOT:-${PROJECT_ROOT}/TESTRUNS}"
SHOW_PROG="${SHOW_PROG:-xdg-open}"

RUN_NAME="$(date +'%Y%m%d-%H%M%S')"
SUITE_ROOT="${TESTRUNS_ROOT}/${RUN_NAME}"
DO_SHOW=no
HAS_UNKNOWN_ARG=no


function print_usage()
{
    cat <<EOF
Usage: $(basename "${0}") [OPTS...]
Where OPTS are of the following:
    -h|--help    Print this message.
    -s|--show    Auto-load the HTML test output on success.
EOF
}


function save_git_state()
{
    pushd "${PROJECT_ROOT}"

    git rev-parse HEAD >"${SUITE_ROOT}"/git_commit
    git diff >"${SUITE_ROOT}"/patch

    popd
}

function update_symlink_latest()
{
    pushd "${TESTRUNS_ROOT}"

    ln -fsT "${RUN_NAME}" latest

    popd
}


function update_symlink_latest_success()
{
    pushd "${TESTRUNS_ROOT}"

    [[ -h "latest_success" ]] && mv latest_success previous_success
    ln -fsT "${RUN_NAME}" latest_success

    popd
}


function remove_mapfiles()
{
    pushd "${SUITE_ROOT}"

    rm map-*-*

    popd
}


function do_coverage_tests()
{
    local test_root="${SUITE_ROOT}/coverage_tests"
    local outfile="${test_root}/out"
    local coverage_file="${test_root}/coverage.info"
    local cleaned_coverage_file="${test_root}/coverage.info.cleaned"
    local coverage_html_root="${test_root}/coverage_html"
    local coverage_summary_file="${test_root}/coverage_summary"

    mkdir "${test_root}"
    pushd "${test_root}"

    # Clear coverage data. (Apparently *.gcda coverage files are generated
    # alongside the binaries, which is sloppy.)
    lcov --directory "${BUILD_ROOT}"/Coverage --zerocounters --rc lcov_branch_coverage=1

    # Run tests
    "${BUILD_ROOT}"/Coverage/test_unit_bst >"${test_root}"/test_unit_bst.out 2>&1
    "${BUILD_ROOT}"/Coverage/test_measure --event-count=1000 --ratios=1,1,1,1,1,1 >"${test_root}"/test_measure.out 2>&1

    # Produce coverage webpages.
    lcov --directory "${BUILD_ROOT}"/Coverage --capture --output-file "${coverage_file}" --rc lcov_branch_coverage=1
    lcov --remove "${coverage_file}" '/usr/*' '*/external/*' '*/test/*' --output-file "${cleaned_coverage_file}" --rc lcov_branch_coverage=1
    genhtml -o "${coverage_html_root}" "${cleaned_coverage_file}" --rc lcov_branch_coverage=1
    {
        lcov -l "${cleaned_coverage_file}" --rc lcov_branch_coverage=1
        echo
        lcov --summary "${cleaned_coverage_file}" --rc lcov_branch_coverage=1
    } 2>&1 |grep -v coverage.info.cleaned >"${coverage_summary_file}"

    # Clear coverage data to have not disrupted contents of BUILD_ROOT/Coverage.
    lcov --directory "${BUILD_ROOT}"/Coverage --zerocounters --rc lcov_branch_coverage=1

    rm map-*-*

    popd 
}


function _exclude_kernel()
{ 
    # Exclude kernel-related function invocations
    sed -e 's/;[^;]*_\[k\]//g'
}

function _omit_offsets()
{
    # Exclude '+0xfff...'-like suffixes which seem to have been added to perf
    # since the instructions at
    # http://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html have been
    # written.
    sed -e 's/\([^ 	]\)+0x[0-9a-f]*/\1/'
}

function generate_map_flamegraphs()
{
    local test_root="${SUITE_ROOT}/map_flamegraphs"
    local outfile="${test_root}/generate_map_flamegraphs.out"
    local stdmap_flamegraph="${test_root}/stdmap_flame.svg"
    local cbbst_flamegraph="${test_root}/cbbst_flame.svg"
    local cbmap_flamegraph="${test_root}/cbmap_flame.svg"
    local foldedfile="${test_root}/folded.tmp"

    if [[ -z "$(which flamegraph.pl)" || -z "$(which stackcollapse-perf.pl)" ]]
    then
        echo "Skipping generate_map_flamegraphs() because FlameGraph scripts not present." 1>&2
        return 1
    fi

    mkdir "${test_root}"
    pushd "${test_root}"

    perf record -F 1000 -a -g -- \
        "${BUILD_ROOT}"/Debug/test_measure --ring-size=134217728 --ratios=1,1,1,1,1,1 >"${outfile}" 2>&1
    perf script |_omit_offsets |stackcollapse-perf.pl --kernel >"${foldedfile}"
    gzip perf.data

    cat "${foldedfile}" |
        _exclude_kernel |
        fgrep stdmap_handle_events |
        flamegraph.pl >"${stdmap_flamegraph}"

    cat "${foldedfile}" |
        _exclude_kernel |
        fgrep cbbst_handle_events |
        flamegraph.pl >"${cbbst_flamegraph}"

    cat "${foldedfile}" |
        _exclude_kernel |
        fgrep cbmap_handle_events |
        grep -v cb_map_consolidate |
        flamegraph.pl >"${cbmap_flamegraph}"

    rm "${test_root}"/map-*-*
    rm "${foldedfile}"

    popd
}


function generate_map_latency_plots()
{
    local test_root="${SUITE_ROOT}/map_latency"
    local outfile="${test_root}/out"

    mkdir "${test_root}"
    pushd "${test_root}"

    "${BUILD_ROOT}"/Release/test_measure --ring-size=134217728 --ratios=1,1,1,1,1,1 >"${outfile}" 2>&1
    "${SCRIPTS_ROOT}/plot_measure.py" "${outfile}"

    ls -l "${test_root}"/map-*-* >"${test_root}/used_maps"
    rm "${test_root}"/map-*-*

    popd
}


function generate_toplevel_html()
{
    pushd "${SUITE_ROOT}"

    {
	cat <<EOF
        <!doctype html>
        <html>
        <head>
        <title>CB Test Run ${RUN_NAME}</title>
        </head>
        <body>
            CB Tests performed on ${RUN_NAME}.
            <br>
            <h1>Coverage</h1>
            <a href="coverage_tests/coverage_html/index.html">Results</a>
            <pre>$(cat "${SUITE_ROOT}/coverage_tests/coverage_summary")</pre>
            <h1>Maps</h1>
            <h2>Latency</h2>
                <object data="map_latency/figure.svg" type="image/svg+xml" width="100%"></object>
            <h2>cb_bst Flamegraph</h2>
                <object data="map_flamegraphs/cbbst_flame.svg" type="image/svg+xml" width="100%"></object>
            <h2>cb_map Flamegraph</h2>
                <object data="map_flamegraphs/cbmap_flame.svg" type="image/svg+xml" width="100%"></object>
        </body>
        </html>
EOF
    } >"${SUITE_ROOT}/index.html"


    popd
}

#Process commandline arguments
while [[ $# -gt 0 ]]
do
    case "$1" in
        -h|--help)
            print_usage
            exit 0
            ;;
        -l|--show-last-success)
            "${SHOW_PROG}" "${TESTRUNS_ROOT}/latest_success/index.html"
            exit 0
            ;;
        -s|--show)
            DO_SHOW=yes
            shift
            ;;
        *)
            echo "Unknown argument \"$1\"." 1>&2
            HAS_UNKNOWN_ARG=yes
            ;;
    esac
    shift
done

if [[ "${HAS_UNKNOWN_ARG}" == "yes" ]]
then
    print_usage 1>&2
    exit 1
fi


# Ensure builds are up to date.
cd "${PROJECT_ROOT}"
make -j4
if [[ $? != 0 ]]
then
    echo "Build failed." 1>&2
    exit 1
fi

# Prepare directory for output of tests.
[[ ! -d "${TESTRUNS_ROOT}" ]] && mkdir "${TESTRUNS_ROOT}"
[[ ! -d "${SUITE_ROOT}" ]] && mkdir "${SUITE_ROOT}"

# Update '${TESTRUNS_ROOT}/latest' symlink.
update_symlink_latest

#Save git state.
save_git_state

# Ensure CB maps get saved with any generated coredumps.
echo 0xFF >/proc/self/coredump_filter

# Perform tests for coverage.
do_coverage_tests

# Generate map flamegraphs.
generate_map_flamegraphs

# Generate map latency plots.
generate_map_latency_plots

# Generate HTML entry point.
generate_toplevel_html

# Update '${TESTRUNS_ROOT}/latest_success' symlink.
update_symlink_latest_success

# Show test report if requested.
if [[ "${DO_SHOW}" == "yes" ]]
then
    "${SHOW_PROG}" "${SUITE_ROOT}/index.html"
fi

