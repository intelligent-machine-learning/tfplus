# Description:
#   build libarchive

licenses(["notice"])  # Apache 2.0

package(default_visibility = ["//visibility:public"])

genrule(
    name = "gen_libarchive_a",
    outs = ["libarchive.a"],
    srcs = glob(["**"]) + [
        "@local_config_cc//:toolchain",
    ],    
    cmd = """
    set -xe
    DEST_DIR=$$PWD/$(@D)
    pushd external/com_libarchive/libarchive-3.6.1/
    mkdir -p builda && pushd builda
    cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DENABLE_EXPAT=OFF \
          -DENABLE_LZMA=OFF \
          -DENABLE_OPENSSL=OFF \
          -DENABLE_LIBB2=OFF \
          -DENABLE_LZ4=OFF \
          -DENABLE_ZSTD=OFF \
          -DENABLE_ZLIB=OFF \
          -DENABLE_BZip2=OFF \
          -DENABLE_LIBXML2=OFF \
          ..
    make -j 8 
    \cp -f libarchive/libarchive.a $$DEST_DIR/libarchive.a
    popd
    popd
    """,
)

cc_library(
    name = "libarchive",
    srcs = ["libarchive.a"],
    hdrs = glob(["libarchive-3.6.1/libarchive/*.h"]),
    includes = ["libarchive",],
    linkstatic = 1,
    visibility = ["//visibility:public"],
    strip_include_prefix = "libarchive-3.6.1",
)



