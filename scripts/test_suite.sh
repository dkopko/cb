#!/usr/bin/env bash
# Copyright 2016 Daniel Kopko.

set -o nounset

PROJECT_ROOT="$(cd "$(dirname "$0")/.." ; pwd)"

#Overrideable settings
BUILD_ROOT="${BUILD_ROOT:-${PROJECT_ROOT}/BUILD}"
TESTRUNS_ROOT="${TESTRUNS_ROOT:-${PROJECT_ROOT}/TESTRUNS}"
SHOW_PROG="${SHOW_PROG:-xdg-open}"

TEST_NAME="$(date +'%Y%m%d-%H%M%S')"
TEST_ROOT="${TESTRUNS_ROOT}/${TEST_NAME}"
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

    git rev-parse HEAD >"${TEST_ROOT}"/git_commit
    git diff >"${TEST_ROOT}"/patch

    popd
}

function update_symlink_latest()
{
    pushd "${TESTRUNS_ROOT}"
    ln -fsT "${TEST_NAME}" latest
    popd
}


function update_symlink_latest_success()
{
    pushd "${TESTRUNS_ROOT}"

    [[ -h "latest_success" ]] && mv latest_success previous_success
    ln -fsT "${TEST_NAME}" latest_success

    popd
}


function remove_mapfiles()
{
    pushd "${TEST_ROOT}"

    rm map-*-*

    popd
}


function do_coverage_tests()
{
    pushd "${TEST_ROOT}"

    # Clear coverage data. (Apparently *.gcda coverage files are generated
    # alongside the binaries, which is sloppy.)
    lcov --directory "${BUILD_ROOT}"/Coverage --zerocounters --rc lcov_branch_coverage=1

    # Run tests
    "${BUILD_ROOT}"/Coverage/test_measure

    # Produce coverage webpages.
    lcov --directory "${BUILD_ROOT}"/Coverage --capture --output-file "${TEST_ROOT}"/coverage.info --rc lcov_branch_coverage=1
    lcov --remove "${TEST_ROOT}"/coverage.info '/usr/*' '*/external/*' '*/test/*' --output-file "${TEST_ROOT}"/coverage.info.cleaned --rc lcov_branch_coverage=1
    genhtml -o "${TEST_ROOT}"/coverage "${TEST_ROOT}"/coverage.info.cleaned --rc lcov_branch_coverage=1
    {
        lcov -l "${TEST_ROOT}"/coverage.info.cleaned
        echo
        lcov --summary "${TEST_ROOT}"/coverage.info.cleaned
    } 2>&1 | grep -v coverage.info.cleaned >"${TEST_ROOT}"/coverage_summary

    # Clear coverage data to have not disrupted contents of BUILD_ROOT/Coverage.
    lcov --directory "${BUILD_ROOT}"/Coverage --zerocounters --rc lcov_branch_coverage=1

    remove_mapfiles

    popd 
}


function generate_toplevel_html()
{
    pushd "${TEST_ROOT}"

    {
	cat <<EOF
        <!doctype html>
        <html>
        <head>
        <title>CB Test Run ${TEST_NAME}</title>
        </head>
        <body>
            CB Tests performed on ${TEST_NAME}.
            <br>
            <a href="coverage/index.html">Coverage Results</a>
            <pre>$(cat "${TEST_ROOT}"/coverage_summary)</pre>
        </body>
        </html>
EOF
    } >"${TEST_ROOT}/index.html"


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


# Prepare directory for output of tests.
[[ ! -d "${TESTRUNS_ROOT}" ]] && mkdir "${TESTRUNS_ROOT}"
[[ ! -d "${TEST_ROOT}" ]] && mkdir "${TEST_ROOT}"


# Update symlinks
update_symlink_latest

#Save git state
save_git_state

# Perform tests.
do_coverage_tests

# Generate HTML entry point.
generate_toplevel_html

# Update symlinks
update_symlink_latest_success

# Show test report if requested.
if [[ "${DO_SHOW}" == "yes" ]]
then
    "${SHOW_PROG}" "${TEST_ROOT}"/index.html
fi

