#!/bin/sh

set -xe

CC="${CXX:-cc}"
CFLAGS="-Wall -Wextra -Wno-pointer-sign -Wno-sign-compare -ggdb -Os -std=c11 -pedantic"

$CC mcsrv.c $CFLAGS -o mcsrv
