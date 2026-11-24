#!/bin/sh
# Runs a program built for the iOS simulator in a booted one, the way CTest runs iOS builds' tests
# (CMAKE_CROSSCOMPILING_EMULATOR): run_ios.sh <tide_ios_run> <program> [arguments...]
#
# The simulator starts a program with its own environment and working directory, so the TIDE_ variables a test
# sets go in by the names simctl hands on (SIMCTL_CHILD_<name>), and <tide_ios_run> (tests/ios_run.c, built with
# the tests) changes to this folder before it becomes the program. The simulator shares the Mac's files, so paths
# are the same inside it. TIDE_IOS_SIMULATOR names the device; the one that's booted by default.
launcher=$1
shift
for name in $(env | sed -n 's/^\(TIDE_[A-Za-z0-9_]*\)=.*/\1/p'); do
    eval "export SIMCTL_CHILD_$name=\"\$$name\""
done
exec xcrun simctl spawn "${TIDE_IOS_SIMULATOR:-booted}" "$launcher" "$PWD" "$@"
