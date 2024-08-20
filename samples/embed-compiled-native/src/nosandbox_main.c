/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>

extern int
add(int a, int b);

int
main()
{
    printf("Nosandbox main: add 3 + 4 = %d\n", add(3, 4));
}
