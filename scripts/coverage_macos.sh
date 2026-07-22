#!/bin/bash -e

# Copyright 2022-Present Couchbase, Inc.
#
# Use of this software is governed by the Business Source License included in
# the file licenses/BSL-Couchbase.txt.  As of the Change Date specified in that
# file, in accordance with the Business Source License, use of this software
# will be governed by the Apache License, Version 2.0, included in the file
# licenses/APL2.txt.

# This script is for generating code coverage results on macOS platform.
# It is currently used by the PR Validation to build the binaries for macOS, run the tests, 
# generate the code coverage results, and push the code coverage results to the PR as a comment.

function usage 
{
  echo "Usage: ${0} [--ce] [--show] [--push]"
}

while [[ $# -gt 0 ]]
do
  key=${1}
  case $key in
      --ce)
      CE="Y"
      ;;
      --show)
      SHOW="Y"
      ;;
      --push)
      PUSH="Y"
      ;;
      *)
      usage
      exit 3
      ;;
  esac
  shift
done

if [ -n "$CE" ]
then
  BUILD_ENTERPRISE="OFF"
else
  BUILD_ENTERPRISE="ON"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd)";
pushd $SCRIPT_DIR/.. > /dev/null

# If calling from a PR Validation :
if [[ -n "$CHANGE_ID" ]]; then
  git submodule update --init --recursive
  if [[ "$BUILD_ENTERPRISE" == "ON" ]]; then
    pushd vendor
    if [[ $CHANGE_TARGET == "master" ]]; then
      BRANCH="main"
    else
      BRANCH=$CHANGE_TARGET
    fi
    rm -rf couchbase-lite-c-ee couchbase-lite-core-EE
    git clone ssh://git@github.com/couchbase/couchbase-lite-c-ee --branch $BRANCH_NAME --recursive --depth 1 couchbase-lite-c-ee || \
      git clone ssh://git@github.com/couchbase/couchbase-lite-c-ee --branch $BRANCH --recursive --depth 1 couchbase-lite-c-ee
    mv couchbase-lite-c-ee/couchbase-lite-core-EE .
    popd
  fi
fi

if [[ "$BUILD_ENTERPRISE" == "ON" ]]; then
  ./scripts/download_vector_search_extension.sh apple
fi

if [[ -n "$KEYCHAIN_PWD" ]]; then
    echo "Unlock keychain ..."
    security -v unlock-keychain -p $KEYCHAIN_PWD $HOME/Library/Keychains/login.keychain-db
else
    echo "Cannot unlock keychain as credentials not found ... "
fi

mkdir -p build_coverage
pushd build_coverage > /dev/null
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_ENTERPRISE=${BUILD_ENTERPRISE} -DCODE_COVERAGE_ENABLED=ON ..    
core_count=`getconf _NPROCESSORS_ONLN`
make -j `expr $core_count + 1`

pushd test > /dev/null
./CBL_C_Tests -r quiet
popd > /dev/null

mkdir -p report

ARCH="x86_64"
FILE_INFO=`file libcblite.dylib`
if [[ "$FILE_INFO" == *"arm64"* ]]; then
  ARCH="arm64"
fi

xcrun llvm-profdata merge -sparse test/default.profraw -o test/default.profdata

xcrun llvm-cov show \
  -format="html" \
  -instr-profile=test/default.profdata \
  -show-line-counts-or-regions \
  -ignore-filename-regex="/vendor/.*" \
  -ignore-filename-regex="/test/.*" \
  -arch $ARCH -object libcblite.dylib \
  -arch $ARCH -object test/CBL_C_Tests \
  -output-dir=report

xcrun llvm-cov export \
  -instr-profile=test/default.profdata \
  -ignore-filename-regex="/vendor/.*" \
  -ignore-filename-regex="/test/.*" \
  -arch $ARCH -object libcblite.dylib \
  -arch $ARCH -object test/CBL_C_Tests \
  > report/coverage.json

if [[ -n "$SHOW" ]]; then
  open report/index.html
fi

if [[ -n "$PUSH" ]] && [[ -n "$CHANGE_ID" ]]; then
  echo "Pushing the coverage result to PR #$CHANGE_ID ..."
  python3 -m venv venv
  source venv/bin/activate 
  pip install -r $SCRIPT_DIR/push_coverage_results_requirements.txt
  python $SCRIPT_DIR/push_coverage_results.py -r report/coverage.json -n $CHANGE_ID
fi

echo "Done : The coverage report was generated at build_coverage/report"

popd > /dev/null

# Cleanup downloaded extension files
rm -rf test/extensions/apple

popd > /dev/null
