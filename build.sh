#!/usr/bin/env bash

# Builds the project in all of its target forms.

SCRIPTHOME="$(cd "$(dirname "$0")" ; pwd)"
PROJECTROOT="${SCRIPTHOME}"
BUILDROOT="${SCRIPTHOME}/BUILD"

cd "${SCRIPTHOME}"

do_cmake_build()
{
    local build_type="$1"
    local build_dir="${BUILDROOT}/${build_type}"


    mkdir -p "${build_dir}"
    pushd "${build_dir}" 2>&1 >/dev/null
    cmake "${PROJECTROOT}" -DCMAKE_BUILD_TYPE="${build_type}"
    make -j4
    popd 2>&1 >/dev/null
}


do_cmake_build Debug
do_cmake_build Release
do_cmake_build RelWithDebInfo
do_cmake_build MinSizeRel

