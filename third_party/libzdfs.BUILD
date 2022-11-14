# Description:
#   build zdfs

licenses(["notice"])  # Apache 2.0

package(default_visibility = ["//visibility:public"])

include_files = [
    "include/zdfs/v1/slice.h",
    "include/zdfs/v1/env.h",
    "include/zdfs/v1/status.h",
    "include/zdfs/pangu/file_system.h",
    "include/zdfs/pangu/error_code.h",
    "include/zdfs/pangu/stat.h",
    "include/zdfs/pangu/options.h",
    "include/zdfs/pangu/file.h",
    "include/zdfs/pangu/admin_env.h",
    "include/zdfs/zdfs.h",
]

lib_files = [
    "build/libzdfs.so",
]

genrule(
    name = "zdfs-dfs-lib",
    outs = lib_files,
    cmd = "$$(pwd)/third_party/build-libzdfs.sh external/com_antfin_libzdfs && cp -f $$(pwd)/external/com_antfin_libzdfs/build/libzdfs.so $(@D)",
    output_to_bindir = 1,
    executable = 1,
    local = 1,
)

cc_library(
    name = "libzdfs",
    srcs = lib_files,
    hdrs = include_files,
    includes = [
        "include",
    ],
    linkstatic = 0,
)
