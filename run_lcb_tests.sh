#!/bin/sh -x
LIBCOUCHBASE_SRC=$1
LIBCOUCHBASE_LIBDIR=$2
TEST_NAME=$3

export LIBCOUCHBASE_VERBOSE_TESTS=1
export LIBCOUCHBASE_TEST_LOOP="libuv"
export LD_LIBRARY_PATH="$PWD:$LIBCOUCHBASE_SRC/.libs:$LD_LIBRARY_PATH"

if [ -n "$DEBUGGER" ]; then
    export LD_PRELOAD=/lib/libpthread.so.0
fi

if [ -z "$TEST_NAME" ]; then
    make -C $LIBCOUCHBASE_SRC/ check
else
    cd $LIBCOUCHBASE_SRC/
    $DEBUGGER ./tests/.libs/$TEST_NAME
fi
