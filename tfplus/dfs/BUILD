package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # Apache 2.0

cc_binary(
    name = "python/ops/_dfs_ops.so",
    srcs = [
        "kernels/dfs/dfs_file_system.cc",
        "kernels/dfs/dfs_file_system.h",
        "ops/dfs_ops.cc",
    ],
    copts = [
        "-std=c++14",
        "-DNDEBUG",
    ],
    linkshared = 1,
    deps = [
        "@local_config_tf//:libtensorflow_framework",
        "@local_config_tf//:tf_header_lib",
        "@com_antfin_libzdfs//:libzdfs",
    ],
)
