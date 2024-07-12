#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

CUR_DIR=$PWD
OUT_DIR=$CUR_DIR/out
REPORT=$CUR_DIR/report.txt
TIME=/usr/bin/time

BENCH_NAME_MAX_LEN=20

SHOOTOUT_CASES="ackermann base64 fib2 gimli heapsort matrix memmove minicsv \
                nestedloop nestedloop2 nestedloop3 random seqhash sieve \
                strchr switch2"

rm -f $REPORT
touch $REPORT

function print_bench_name()
{
    name=$1
    echo -en "$name" >> $REPORT
    name_len=${#name}
    if [ $name_len -lt $BENCH_NAME_MAX_LEN ]
    then
        spaces=$(( $BENCH_NAME_MAX_LEN - $name_len ))
        for i in $(eval echo "{1..$spaces}"); do echo -n " " >> $REPORT; done
    fi
}

echo "Start to run cases, the result is written to report.txt"

#run benchmarks
cd $OUT_DIR
echo -en "\t\t\t\t\t  gcc-native\twasm-mem32\twasm-mem64\twasm-mem64-nosandbox\n" >> $REPORT

for t in $SHOOTOUT_CASES
do
    print_bench_name $t

    echo "run $t of gcc native .."
    echo -en "\t" >> $REPORT
    $TIME -f "real-%e-time" ./${t} 2>&1 | grep "real-.*-time" | awk -F '-' '{ORS=""; print $2}' >> $REPORT

    echo "run $t of wasm memory32 .."
    echo -en "\t" >> $REPORT
    $TIME -f "real-%e-time" ./${t}_mem32 2>&1 | grep "real-.*-time" | awk -F '-' '{ORS=""; print $2}' >> $REPORT

    echo "run $t of wasm memory64 .."
    echo -en "\t" >> $REPORT
    $TIME -f "real-%e-time" ./${t}_mem64 2>&1 | grep "real-.*-time" | awk -F '-' '{ORS=""; print $2}' >> $REPORT

    echo "run $t of wasm memory64 nosandboxmode .."
    echo -en "\t" >> $REPORT
    $TIME -f "real-%e-time" ./${t}_mem64_nosandbox 2>&1 | grep "real-.*-time" | awk -F '-' '{ORS=""; print $2}' >> $REPORT

    echo -en "\n" >> $REPORT
done
