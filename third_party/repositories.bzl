load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def prepare_six():
  http_archive(
      name = "six_archive",
      build_file = "//third_party:six.BUILD",
      sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
      strip_prefix = "six-1.10.0",
  #       system_build_file = clean_dep("//third_party/systemlibs:six.BUILD"),
      urls = [
          "https://mirror.bazel.build/pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz",
          "https://pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz",
      ],
  )

  native.bind(
          name = "six",
          actual = "@six_archive//:six",
  )