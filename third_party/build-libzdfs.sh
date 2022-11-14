#!/bin/bash

set -e

parent_dir=`dirname $0`

CWD=`cd $parent_dir && pwd`
ZDFS_SRC=${1?"zdfs source path"}

cd $ZDFS_SRC

DEFAULT_GCC_VERSION=`gcc -dumpversion`
DEFAULT_CC_VERSION=`cc -dumpversion`
REQUIRE_GCC_VERSION="4.9.2"
STATIC_LIBSTDC_PATCH=$CWD/libzdfs-patch-static-link-libstdc++.patch

# Determine the right gcc version to use
ret=`echo -e "${DEFAULT_GCC_VERSION}\n${REQUIRE_GCC_VERSION}" | sort -V | head -n1`
if [[ $ret == $REQUIRE_GCC_VERSION ]] ; then
    # $REQUIRE_GCC_VERSION <= $DEFAULT_GCC_VERSION
    if [[ $DEFAULT_CC_VERSION != $DEFAULT_GCC_VERSION ]] ; then
        # `cc -dumpversion` differs from `gcc -dumpversion`
        CMAKE_GCC_OPT="-DCMAKE_C_COMPILER=`which gcc` -DCMAKE_CXX_COMPILER=`which g++`"
    else
        CMAKE_GCC_OPT=
    fi
elif test -f /usr/local/gcc-4.9.2/bin/gcc ; then
    # $REQUIRE_GCC_VERSION > $DEFAULT_GCC_VERSION
    CMAKE_GCC_OPT="-DCMAKE_C_COMPILER=/usr/local/gcc-4.9.2/bin/gcc -DCMAKE_CXX_COMPILER=/usr/local/gcc-4.9.2/bin/g++"
    # only do static link libstdc++ if compiling use gcc-4.9.2
    patch -p1 < $STATIC_LIBSTDC_PATCH
else
    echo "Can't find suitable gcc version to build libzdfs. ABORT!"
    exit 1
fi

./scripts/update-dfs-sdk.sh
mkdir -p build && cd build
cmake .. -DTYPE_SDK=dfs $CMAKE_GCC_OPT
make -j 8
