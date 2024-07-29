#!/usr/bin/env bash

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

function DEBUG() {
  [[ -n $(env | grep "\<DEBUG\>") ]] && $@
}
DEBUG set -exv pipefail

function help()
{
    echo "test_w2n.sh [options]"
    echo "-c clean previous test results, not start test"
    echo "-s {suite_name} test only one suite (spec)"
    echo "-m set compile target of native binary(x86_64|x86_32|armv7|armv7_vfp|thumbv7|thumbv7_vfp|"
    echo "                                       riscv32|riscv32_ilp32f|riscv32_ilp32d|riscv64|"
    echo "                                       riscv64_lp64f|riscv64_lp64d|aarch64|aarch64_vfp)"
    echo "-S enable SIMD feature"
    echo "-W enable memory64 feature"
    echo "-b use the wabt binary release package instead of compiling from the source code"
    echo "-P run the spec test parallelly"
    echo "-Q enable qemu"
    echo "-F set the firmware path used by qemu"
    echo "-C enable code coverage collect"
    echo "-j set the platform to test"
    echo "-T set sanitizer to use in tests(ubsan|tsan|asan)"
}

OPT_PARSED=""
WABT_BINARY_RELEASE="NO"
#default target
TARGET="X86_64"
COLLECT_CODE_COVERAGE=0
ENABLE_SIMD=0
ENABLE_MEMORY64=0
# test case array
TEST_CASE_ARR=()
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    PLATFORM=windows
    PYTHON_EXE=python
else
    PLATFORM=$(uname -s | tr A-Z a-z)
    PYTHON_EXE=python3
fi
PARALLELISM=0
ENABLE_QEMU=0
QEMU_FIRMWARE=""
TARGET_LIST=("AARCH64" "AARCH64_VFP" "ARMV7" "ARMV7_VFP" "THUMBV7" "THUMBV7_VFP" \
             "RISCV32" "RISCV32_ILP32F" "RISCV32_ILP32D" "RISCV64" "RISCV64_LP64F" "RISCV64_LP64D")

while getopts ":s:cbm:CSWPQF:j:T:" opt
do
    OPT_PARSED="TRUE"
    case $opt in
        s)
        TEST_CASE_ARR+=($OPTARG)
        # get next suite if there are multiple vaule in -s
        eval "nxarg=\${$((OPTIND))}"
        # just get test cases, loop until the next symbol '-'
        # IN  ====>  -s spec unit -t fast-classic
        # GET ====>  spec unit
        while [[ "${nxarg}" != -* && ${nxarg} ]];
        do
            TEST_CASE_ARR+=(${nxarg})
            OPTIND=$((OPTIND+1))
            eval "nxarg=\${$((OPTIND))}"
        done
        echo "test following cases: ${TEST_CASE_ARR[@]}"
        ;;
        c)
        read -t 5 -p "Are you sure to delete all reports. y/n    " cmd
        if [[ $cmd == "y" ]];then
            rm -fr workspace/report/*
            rm -fr /tmp/*.wasm /tmp/*.wast /tmp/*.w2n.o /tmp/*.w2n
            echo "cleaned all reports and temp files"
        fi
        exit 0;;
        b)
        WABT_BINARY_RELEASE="YES"
        echo "use a WABT binary release instead of compiling from source code"
        ;;
        m)
        echo "set compile target of native binary" ${OPTARG}
        TARGET=$(echo "$OPTARG" | tr '[a-z]' '[A-Z]') # set target to uppercase if input x86_32 or x86_64 --> X86_32 and X86_64
        ;;
        W)
        echo "enable wasm64(memory64) feature"
        ENABLE_MEMORY64=1
        ;;
        C)
        echo "enable code coverage"
        COLLECT_CODE_COVERAGE=1
        ;;
        S)
        echo "enable SIMD feature"
        ENABLE_SIMD=1
        ;;
        P)
        PARALLELISM=1
        ;;
        Q)
        echo "enable QEMU"
        ENABLE_QEMU=1
        ;;
        F)
        echo "QEMU firmware" ${OPTARG}
        QEMU_FIRMWARE=${OPTARG}
        ;;
        j)
        echo "test platform " ${OPTARG}
        PLATFORM=${OPTARG}
        ;;
        T)
        echo "sanitizer is " ${OPTARG}
        W2N_BUILD_SANITIZER=${OPTARG}
        ;;
        ?)
        help
        exit 1
        ;;
    esac
done

# Parameters are not allowed, use options instead
if [ -z "$OPT_PARSED" ];
then
    if [ ! -z "$1" ];
    then
        help
        exit 1
    fi
fi

mkdir -p workspace
cd workspace

readonly WORK_DIR=$PWD

readonly DATE=$(date +%Y-%m-%d_%H:%M:%S)
readonly REPORT_DIR=${WORK_DIR}/report/${DATE}
mkdir -p ${REPORT_DIR}

readonly W2N_DIR=${WORK_DIR}/../../..
readonly WASM2NATIVE_CMD="${W2N_DIR}/wasm2native-compiler/build/wasm2native"

function setup_wabt()
{
    WABT_VERSION=1.0.34
    if [ ${WABT_BINARY_RELEASE} == "YES" ]; then
        echo "download a binary release and install"
        local WAT2WASM=${WORK_DIR}/wabt/out/gcc/Release/wat2wasm
        if [ ! -f ${WAT2WASM} ]; then
            case ${PLATFORM} in
                cosmopolitan)
                    ;;
                linux)
                    WABT_PLATFORM=ubuntu
                    ;;
                darwin)
                    WABT_PLATFORM=macos-12
                    ;;
                windows)
                    WABT_PLATFORM=windows
                    ;;
                *)
                    echo "wabt platform for ${PLATFORM} in unknown"
                    exit 1
                    ;;
            esac
            if [ ! -f /tmp/wabt-${WABT_VERSION}-${WABT_PLATFORM}.tar.gz ]; then
                curl -L \
                    https://github.com/WebAssembly/wabt/releases/download/${WABT_VERSION}/wabt-${WABT_VERSION}-${WABT_PLATFORM}.tar.gz \
                    -o /tmp/wabt-${WABT_VERSION}-${WABT_PLATFORM}.tar.gz
            fi

            cd /tmp \
            && tar zxf wabt-${WABT_VERSION}-${WABT_PLATFORM}.tar.gz \
            && mkdir -p ${WORK_DIR}/wabt/out/gcc/Release/ \
            && install wabt-${WABT_VERSION}/bin/* ${WORK_DIR}/wabt/out/gcc/Release/ \
            && cd -
        fi
    else
        echo "download source code and compile and install"
        if [ ! -d "wabt" ];then
            echo "wabt not exist, clone it from github"
            git clone --recursive https://github.com/WebAssembly/wabt
        fi
        echo "upate wabt"
        cd wabt
        git fetch origin
        git reset --hard origin/main
        git checkout tags/${WABT_VERSION} -B ${WABT_VERSION}
        cd ..
        make -C wabt gcc-release -j 4 || exit 1
    fi
}

function spec_test()
{
    local RUNNING_MODE="$1"

    echo "Now start spec tests"
    touch ${REPORT_DIR}/spec_test_report.txt

    cd ${WORK_DIR}

    # update basic test cases
    echo "downloading spec test cases..."

    rm -rf spec
    if [[ ${ENABLE_MEMORY64} == 1 ]]; then
        echo "checkout spec for memory64 proposal"

        # check spec test cases for memory64
        git clone -b main --single-branch https://github.com/WebAssembly/memory64.git spec
        pushd spec

        # Reset to commit: "Merge remote-tracking branch 'upstream/main' into merge2"
        git reset --hard 48e69f394869c55b7bbe14ac963c09f4605490b6
        git checkout 044d0d2e77bdcbe891f7e0b9dd2ac01d56435f0b -- test/core/elem.wast test/core/data.wast
        git apply ../../spec-test-script/memory64_ignore_cases.patch || exit 1
    else
        echo "checkout spec for default proposal"

        git clone -b main --single-branch https://github.com/WebAssembly/spec
        pushd spec

        # Apr 3, 2024 [js-api] Integrate with the ResizableArrayBuffer proposal (#1300)
        git reset --hard bc76fd79cfe61033d7f4ad4a7e8fc4f996dc5ba8
        git apply ../../spec-test-script/ignore_cases.patch || exit 1
        if [[ ${ENABLE_SIMD} == 1 ]]; then
            git apply ../../spec-test-script/simd_ignore_cases.patch || exit 1
        fi
    fi

    popd
    echo $(pwd)

    setup_wabt

    ln -sf ${WORK_DIR}/../spec-test-script/all.py .
    ln -sf ${WORK_DIR}/../spec-test-script/runtest.py .

    local ARGS_FOR_SPEC_TEST=""

    if [[ ${ENABLE_SIMD} == 1 ]]; then
        ARGS_FOR_SPEC_TEST+="-S "
    fi

    # set the current running target
    ARGS_FOR_SPEC_TEST+="-m ${TARGET} "

    if [[ ${PARALLELISM} == 1 ]]; then
        ARGS_FOR_SPEC_TEST+="--parl "
    fi

    if [[ 1 == ${ENABLE_MEMORY64} ]]; then
        ARGS_FOR_SPEC_TEST+="--memory64 "
    fi

    if [[ ${ENABLE_QEMU} == 1 ]]; then
        ARGS_FOR_SPEC_TEST+="--qemu "
        ARGS_FOR_SPEC_TEST+="--qemu-firmware ${QEMU_FIRMWARE} "
    fi

    if [[ ${PLATFORM} == "windows" ]]; then
        ARGS_FOR_SPEC_TEST+="--no-pty "
    fi

    # set log directory
    ARGS_FOR_SPEC_TEST+="--log ${REPORT_DIR}"

    pushd ${WORK_DIR} > /dev/null 2>&1
    echo "${PYTHON_EXE} ./all.py ${ARGS_FOR_SPEC_TEST} | tee -a ${REPORT_DIR}/spec_test_report.txt"
    ${PYTHON_EXE} ./all.py ${ARGS_FOR_SPEC_TEST} | tee -a ${REPORT_DIR}/spec_test_report.txt
    if [[ ${PIPESTATUS[0]} -ne 0 ]];then
        echo -e "\nspec tests FAILED" | tee -a ${REPORT_DIR}/spec_test_report.txt
        exit 1
    fi
    popd > /dev/null 2>&1

    echo -e "\nFinish spec tests" | tee -a ${REPORT_DIR}/spec_test_report.txt
}

function build_wasm2native()
{
    if [[ "${TARGET_LIST[*]}" =~ "${TARGET}" ]]; then
        echo "suppose wasm2native compiler is already built"
        return
    fi

    echo "Build wasm2native compiler"
    cd ${W2N_DIR}/wasm2native-compiler \
        && ./build_llvm.sh \
        && if [ -d build ]; then rm -r build/*; else mkdir build; fi \
        && cd build \
        && cmake .. -DCOLLECT_CODE_COVERAGE=${COLLECT_CODE_COVERAGE} \
        && make -j 4
    cd ${W2N_DIR}/wasm2native-vmlib \
        && if [ -d build ]; then rm -r build/*; else mkdir build; fi \
        && cd build \
        && cmake .. -DW2N_BUILD_WASM_APPLICATION=1 \
        && make -j 4
}

### Need to add a test suite?
### The function name should be ${suite_name}_test
# function xxx_test()
# {
#
# }

function collect_coverage()
{
    if [[ ${COLLECT_CODE_COVERAGE} == 1 ]]; then
        ln -sf ${WORK_DIR}/../spec-test-script/collect_coverage.sh ${WORK_DIR}

        CODE_COV_FILE=""
        if [[ -z "${CODE_COV_FILE}" ]]; then
            CODE_COV_FILE="${WORK_DIR}/wasm2native.lcov"
        else
            CODE_COV_FILE="${CODE_COV_FILE}"
        fi

        pushd ${WORK_DIR} > /dev/null 2>&1
        echo "Collect code coverage of wasm2native"
        ./collect_coverage.sh ${CODE_COV_FILE} ${W2N_DIR}/wasm2native-compiler/build
        popd > /dev/null 2>&1
    else
        echo "code coverage isn't collected"
    fi
}

function trigger()
{
    build_wasm2native
    for suite in "${TEST_CASE_ARR[@]}"; do
        $suite"_test"
    done
    # collect_coverage llvm-aot
}

# if collect code coverage, ignore -s, test all test cases.
if [[ $TEST_CASE_ARR ]];then
    trigger || (echo "TEST FAILED"; exit 1)
else
    # test all suite, ignore polybench and libsodium because of long time cost
    TEST_CASE_ARR=("spec")
    trigger || (echo "TEST FAILED"; exit 1)
fi

echo -e "Test finish. Reports are under ${REPORT_DIR}"
DEBUG set +exv pipefail
echo "TEST SUCCESSFUL"
exit 0
