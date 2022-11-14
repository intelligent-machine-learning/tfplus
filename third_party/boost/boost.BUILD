package(default_visibility = ["//visibility:public"])

licenses(["notice"])

load("@org_tfplus//third_party/boost:boost.bzl", "boost_library")

cc_library(
    name = 'headers',
    visibility = ["//visibility:public"],
    # includes: list of include dirs added to the compile line
    includes = [
        ".",
    ],
    hdrs = glob([
        "boost/**/*.h",
        "boost/**/*.hpp",
        "boost/**/*.ipp",
    ]),
)

boost_library(
    name = 'system',
    deps = [
        ':headers',
    ],
)
