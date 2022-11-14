licenses(["notice"])  # Apache 2.0

package(default_visibility = ["//visibility:public"])

load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")

proto_library(
    name = "checkpoint_state_ext_proto",
    srcs = ["python/training/checkpoint_state_extend.proto"],
)

py_proto_library(
    name = "checkpoint_state_extend_py_pb2",
    srcs = ["python/training/checkpoint_state_extend.proto"],
)

cc_library(
    name = "kv_variable_lib",
    hdrs = [
        #"kernels/hashmap.h",
        #"kernels/embedding_value.h",
        #"kernels/embedding_service.h",
        #"kernels/kv_variable_interface.h",
        #"kernels/kv_variable.h",
        #"kernels/kv_variable.hpp",
        #"kernels/mutex.h",
        "kernels/utility.h",
        #"kernels/kv_variable_cwise_op.h",
        #"kernels/tensor_data_buffer.h",
        "kernels/naming.h",
        "kernels/tensor_bundle.h",
        "utils/utils.h",
		"kernels/byte_swap.h",
    ],
    srcs = [
        "kernels/byte_swap.cc",
        "kernels/utility.cc",
        #"kernels/kv_variable_ops.cc",
        #"kernels/training_ops.cc",
        #"kernels/segment_reduction_ops.cc",
        #"ops/kv_variable_ops.cc",
        #"ops/training_ops.cc",
        "ops/math_ops.cc",
        "utils/utils.cc",
        "kernels/naming.cc",
        #"kernels/tensor_bundle.cc",
        #"kernels/save_restore_v3_ops.cc",
    ],
    linkstatic = 1,
    copts = [
        "-std=c++14",
        "-fPIC",
        "-DNDEBUG",
        "-D_GLIBCXX_USE_CXX11_ABI=0",
        "-D__STDC_FORMAT_MACROS",
    ],
    deps = [
        "@local_config_tf//:libtensorflow_framework",
        "@local_config_tf//:tf_header_lib",
        "@tbb",
        #"@libcuckoo",
        "@sparsehash",
        "@murmurhash",
        "@farmhash",
        #"@phstore_lib//:phstore_lib",
        "@com_libarchive//:libarchive",
    ]
)

cc_binary(
    name = "python/ops/_kv_variable_ops.so",
    copts = [
        "-std=c++14",
        "-fPIC",
    ],
    linkopts=[
        "-pthread",
        "-lssl",
        "-lcrypto",
        "-lbz2",
        "-llzma",
        "-Xlinker",
        "-zmuldefs",
        "-Wl,--version-script=$(location //third_party:libaio.lds)",
    ],
    linkshared = 1,
    deps = [
        ":kv_variable_lib",
        "@local_config_tf//:libtensorflow_framework",
        "@local_config_tf//:tf_header_lib",
        "//third_party:libaio.lds",
    ],
)

cc_test(
    name = "kv_variable_test",
    size = "small",
    srcs = [
        #"kernels/kv_variable_test.cc",
        "kernels/tbb_lib_test.cc",
        "kernels/utility_test.cc",
    ],
    linkopts = [
        "-lssl",
        "-lcrypto",
        "-lbz2",
        "-llzma"
    ],
    copts = [
        "-DNDEBUG",
        "-std=c++14",
    ],
    deps = [
        ":kv_variable_lib",
        "@local_config_tf//:libtensorflow_framework",
        "@local_config_tf//:tf_header_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "kv_variable_opdef",
    srcs = [
        #"ops/kv_variable_ops.cc",
        #"ops/training_ops.cc",
    ],
    copts = [
        "-std=c++14",
        "-DNDEBUG",
    ],
    deps = [
        "@local_config_tf//:tf_header_lib",
    ],
)

cc_library(
    name = "kv_variable_interface",
    hdrs = [
        #"kernels/kv_variable_interface.h",
        #"kernels/tensor_bundle.h",
        "kernels/naming.h",
        #"kernels/mutex.h",
    ],
    copts = [
        "-std=c++14",
        "-DNDEBUG",
    ],
    deps = [
        "@local_config_tf//:tf_header_lib",
        "@local_config_tf//:libtensorflow_framework",
        "@tbb",
    ],
)
