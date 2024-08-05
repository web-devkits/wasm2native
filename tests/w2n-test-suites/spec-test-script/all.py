#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

import argparse
import multiprocessing as mp
import platform
import pathlib
import subprocess
import sys
import time

'''
python3 runtest.py --wast2wasm ./wabt/out/gcc/Release/wat2wasm --wasm2native-compiler
    ../../../wasm2native-compiler/build/wasm2native
    --vmlib-file ../../../wasm2native-vmlib/build/libvmlib.a
    --target x86_64 wast_file
'''

def exe_file_path(base_path: str) -> str:
    if platform.system().lower() == "windows":
        base_path += ".exe"
    return base_path

PLATFORM_NAME = platform.uname().system.lower()
SPEC_TEST_DIR = "spec/test/core"
WAST2WASM_CMD = exe_file_path("./wabt/out/gcc/Release/wat2wasm")
SPEC_INTERPRETER_CMD = "spec/interpreter/wasm"
WASM2NATIVE_CMD = "../../../wasm2native-compiler/build/wasm2native"
VMLIB_FILE = "../../../wasm2native-vmlib/build/libvmlib.a"

AVAILABLE_TARGETS = [
    "I386",
    "X86_32",
    "X86_64",
    "AARCH64",
    "AARCH64_VFP",
    "ARMV7",
    "ARMV7_VFP",
    "RISCV32",
    "RISCV32_ILP32F",
    "RISCV32_ILP32D",
    "RISCV64",
    "RISCV64_LP64F",
    "RISCV64_LP64D",
    "THUMBV7",
    "THUMBV7_VFP",
]

def ignore_the_case(
    case_name,
    target,
    simd_flag=False,
    memory64_flag=False,
    qemu_flag=False,
):

    if case_name in ["comments", "inline-module", "names"]:
        return True

    if case_name in ["imports", "linking", "simd_linking"]:
        return True

    # wasm2native don't support some opcodes, 
    # for instance, ref-types and some bulk memory
    if case_name in [
        # ref-types
        "ref_func",
        "ref_null",
        "ref_is_null",
        "table_get",
        "table_set",
        "table_init",
        "table_copy",
        "elem_drop",
        "table_size",
        "table_grow",
        "table_fill",
        # TODO: memory_fill and memory_copy too?
        # some bulk memory opcodes
        "bulk",
        "memory_init",
        "data_drop",
    ]:
        return True

    # Note: x87 doesn't preserve sNaN and makes some relevant tests fail
    if "i386" == target and case_name in ["float_exprs", "conversions"]:
        return True

    if qemu_flag:
        if case_name in [
            "f32_bitwise",
            "f64_bitwise",
            "loop",
            "f64",
            "f64_cmp",
            "conversions",
            "f32",
            "f32_cmp",
            "float_exprs",
            "float_misc",
            "select",
            "memory_grow",
        ]:
            return True

    return False


def preflight_check():
    if not pathlib.Path(SPEC_TEST_DIR).resolve().exists():
        print(f"Can not find {SPEC_TEST_DIR}")
        return False

    if not pathlib.Path(WAST2WASM_CMD).resolve().exists():
        print(f"Can not find {WAST2WASM_CMD}")
        return False

    if not pathlib.Path(WASM2NATIVE_CMD).resolve().exists():
        print(f"Can not find {WASM2NATIVE_CMD}")
        return False

    if not pathlib.Path(VMLIB_FILE).resolve().exists():
        print(f"Can not find {VMLIB_FILE}")
        return False

    return True


def test_case(
    case_path,
    target,
    simd_flag=False,
    clean_up_flag=True,
    verbose_flag=True,
    memory64_flag=False,
    qemu_flag=False,
    qemu_firmware="",
    log="",
    no_pty=False
):
    CMD = [sys.executable, "runtest.py"]
    CMD.append("--wast2wasm")
    CMD.append(WAST2WASM_CMD)
    if no_pty:
        CMD.append("--no-pty")
    CMD.append("--wasm2native-compiler")
    CMD.append(WASM2NATIVE_CMD)
    CMD.append("--vmlib-file")
    CMD.append(VMLIB_FILE)

    CMD.append("--target")
    CMD.append(target)

    if simd_flag:
        CMD.append("--simd")

    if qemu_flag:
        CMD.append("--qemu")
        CMD.append("--qemu-firmware")
        CMD.append(qemu_firmware)

    if not clean_up_flag:
        CMD.append("--no_cleanup")

    if memory64_flag:
        CMD.append("--memory64")

    if log != "":
        CMD.append("--log-dir")
        CMD.append(log)

    case_path = pathlib.Path(case_path).resolve()
    case_name = case_path.stem

    CMD.append(str(case_path))
    print(f"============> run {case_name} ", end="")
    #print(f"CMD: {CMD}", end="")
    with subprocess.Popen(
        CMD,
        bufsize=1,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    ) as p:
        try:
            case_last_words = []
            while not p.poll():
                output = p.stdout.readline()

                if not output:
                    break

                if verbose_flag:
                    print(output, end="")
                else:
                    if len(case_last_words) == 16:
                        case_last_words.pop(0)
                    case_last_words.append(output)

            p.wait(60)

            if p.returncode:
                print(f"failed with a non-zero return code {p.returncode}")
                if not verbose_flag:
                    print(
                        f"\n==================== LAST LOG of {case_name} ====================\n"
                    )
                    print("".join(case_last_words))
                    print("\n==================== LAST LOG END ====================\n")
                raise Exception(case_name)
            else:
                print("successful")
                return True
        except subprocess.CalledProcessError:
            print("failed with CalledProcessError")
            raise Exception(case_name)
        except subprocess.TimeoutExpired:
            print("failed with TimeoutExpired")
            raise Exception(case_name)


def test_suite(
    target,
    simd_flag=False,
    clean_up_flag=True,
    verbose_flag=True,
    memory64_flag=False,
    parl_flag=False,
    qemu_flag=False,
    qemu_firmware="",
    log="",
    no_pty=False,
):
    suite_path = pathlib.Path(SPEC_TEST_DIR).resolve()
    if not suite_path.exists():
        print(f"can not find spec test cases at {suite_path}")
        return False

    case_list = sorted(suite_path.glob("*.wast"))
    if simd_flag:
        simd_case_list = sorted(suite_path.glob("simd/*.wast"))
        case_list.extend(simd_case_list)

    # ignore based on command line options
    filtered_case_list = []
    for case_path in case_list:
        case_name = case_path.stem
        if not ignore_the_case(
            case_name,
            target,
            simd_flag,
            memory64_flag,
            qemu_flag,
        ):
            filtered_case_list.append(case_path)
        else:
            print(f"---> skip {case_name}")
    print(f"---> {len(case_list)} ---filter--> {len(filtered_case_list)}")
    case_list = filtered_case_list

    case_count = len(case_list)
    failed_case = 0
    successful_case = 0

    if parl_flag:
        print(f"----- Run the whole spec test suite on {mp.cpu_count()} cores -----")
        with mp.Pool() as pool:
            results = {}
            for case_path in case_list:
                results[case_path.stem] = pool.apply_async(
                    test_case,
                    [
                        str(case_path),
                        target,
                        simd_flag,
                        clean_up_flag,
                        verbose_flag,
                        memory64_flag,
                        qemu_flag,
                        qemu_firmware,
                        log,
                        no_pty,
                    ],
                )

            for case_name, result in results.items():
                try:
                    if qemu_flag:
                        # 60 min / case, testing on QEMU may be very slow
                        result.wait(7200)
                    else:
                        # 5 min / case
                        result.wait(300)
                    if not result.successful():
                        failed_case += 1
                    else:
                        successful_case += 1
                except mp.TimeoutError:
                    print(f"{case_name} meets TimeoutError")
                    failed_case += 1
    else:
        print(f"----- Run the whole spec test suite -----")
        for case_path in case_list:
            print(case_path)
            try:
                test_case(
                    str(case_path),
                    target,
                    simd_flag,
                    clean_up_flag,
                    verbose_flag,
                    memory64_flag,
                    qemu_flag,
                    qemu_firmware,
                    log,
                    no_pty,
                )
                successful_case += 1
            except Exception as e:
                failed_case += 1
                raise e

    print(
        f"IN ALL {case_count} cases: {successful_case} PASS, {failed_case} FAIL, {case_count - successful_case - failed_case} SKIP"
    )

    return 0 == failed_case


def main():
    parser = argparse.ArgumentParser(description="run the whole spec test suite")

    parser.add_argument(
        "-m",
        choices=AVAILABLE_TARGETS,
        type=str,
        dest="target",
        default="X86_64",
        help="Specify Target ",
    )
    parser.add_argument(
        "-S",
        action="store_true",
        default=False,
        dest="simd_flag",
        help="Running with the SIMD feature",
    )
    parser.add_argument(
        "--no_clean_up",
        action="store_false",
        default=True,
        dest="clean_up_flag",
        help="Does not remove tmpfiles. But it will be enabled while running parallelly",
    )
    parser.add_argument(
        "--parl",
        action="store_true",
        default=False,
        dest="parl_flag",
        help="To run whole test suite parallelly",
    )
    parser.add_argument(
        "--qemu",
        action="store_true",
        default=False,
        dest="qemu_flag",
        help="To run whole test suite in qemu",
    )
    parser.add_argument(
        "--qemu-firmware",
        default="",
        dest="qemu_firmware",
        help="Firmware required by qemu",
    )
    parser.add_argument(
        "--log",
        default="",
        dest="log",
        help="Log directory",
    )
    parser.add_argument(
        "--quiet",
        action="store_false",
        default=True,
        dest="verbose_flag",
        help="Close real time output while running cases, only show last words of failed ones",
    )
    parser.add_argument(
        "--memory64",
        action="store_true",
        default=False,
        dest="memory64_flag",
        help="Running with memory64 feature",
    )
    parser.add_argument(
        "cases",
        metavar="path_to__case",
        type=str,
        nargs="*",
        help=f"Specify all wanted cases. If not the script will go through all cases under {SPEC_TEST_DIR}",
    )
    parser.add_argument('--no-pty', action='store_true',
        help="Use direct pipes instead of pseudo-tty")

    options = parser.parse_args()

    # Convert target to lower case for internal use, e.g. X86_64 -> x86_64
    # target is always exist, so no need to check it
    options.target = options.target.lower()

    if options.target == "x86_32":
        options.target = "i386"

    if not preflight_check():
        return False

    if not options.cases:
        if options.parl_flag:
            # several cases might share the same workspace/tempfile at the same time
            # so, disable it while running parallelly
            options.clean_up_flag = False
            options.verbose_flag = False

        start = time.time_ns()
        ret = test_suite(
            options.target,
            options.simd_flag,
            options.clean_up_flag,
            options.verbose_flag,
            options.memory64_flag,
            options.parl_flag,
            options.qemu_flag,
            options.qemu_firmware,
            options.log,
            options.no_pty
        )
        end = time.time_ns()
        print(
            f"It takes {((end - start) / 1000000):,} ms to run test_suite {'parallelly' if options.parl_flag else ''}"
        )
    else:
        try:
            for case in options.cases:
                test_case(
                    case,
                    options.target,
                    options.simd_flag,
                    options.clean_up_flag,
                    options.verbose_flag,
                    options.memory64_flag,
                    options.qemu_flag,
                    options.qemu_firmware,
                    options.log,
                    options.no_pty,
                )
            else:
                ret = True
        except Exception:
            ret = False

    return ret


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
