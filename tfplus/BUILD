licenses(["notice"])  # Apache 2.0

package(default_visibility = ["//visibility:public"])


cc_binary(
    name = "libtfplus.so",
    copts = [
        "-std=c++14",
        "-DNDEBUG",
    ],
    linkopts = [
      '-static-libstdc++',
      '-Wl,--version-script,$(location //third_party:libaio.lds)',
    ],
    linkshared = 1,
    deps = [
        "//tfplus/kv_variable:python/ops/_kv_variable_ops.so",
        #"//tfplus/data:decode_ops",
        "//third_party:libaio.lds",
    ],
)

cc_binary(
    name = "libtfplus_opdef.so",
    copts = [
        "-std=c++14",
        "-DNDEBUG",
    ],
    linkshared = 1,
    deps = [
        "//tfplus/kv_variable:kv_variable_opdef",
        # "//tfplus/data:decode_opdef",
    ],
)
