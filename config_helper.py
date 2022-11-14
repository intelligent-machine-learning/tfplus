# Copyright 2022 The TF-plus Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# =============================================================================
# pylint: disable=redefined-outer-name
"""Config Utility to write .bazelrc based on tensorflow."""
from __future__ import print_function
import re
import os
import sys
import tensorflow as tf


class Bcolors:
  HEADER = "\033[95m"
  OKBLUE = "\033[94m"
  OKCYAN = "\033[96m"
  OKGREEN = "\033[92m"
  WARNING = "\033[93m"
  FAIL = "\033[91m"
  ENDC = "\033[0m"
  BOLD = "\033[1m"
  UNDERLINE = "\033[4m"

def write_cuda():
  """ Add cuda config """
  try:
    with open(".bazelrc", "a") as bazel_rc:
      cuda_config = """

# link math in cuda 
build --linkopt=-lm
############################# cuda configuration from native tensorflow 1.13
# This config refers to building with CUDA available. It does not necessarily
# mean that we build CUDA op kernels.
build:using_cuda --define=using_cuda=true
build:using_cuda --action_env TF_NEED_CUDA=1
build:using_cuda --crosstool_top=@local_config_cuda//crosstool:toolchain

# This config refers to building CUDA op kernels with nvcc.
build:cuda --config=using_cuda
build:cuda --define=using_cuda_nvcc=true

# This config refers to building CUDA op kernels with clang.
build:cuda_clang --config=using_cuda
build:cuda_clang --define=using_cuda_clang=true
build:cuda_clang --define=using_clang=true

# dbg config, as a shorthand for '--config=opt -c dbg'
build:dbg --config=opt -c dbg
# for now, disable arm_neon. see: https://github.com/tensorflow/tensorflow/issues/33360
build:dbg --cxxopt -DTF_LITE_DISABLE_X86_NEON
# AWS SDK must be compiled in release mode. see: https://github.com/tensorflow/tensorflow/issues/37498
build:dbg --copt -DDEBUG_BUILD

# Options from .tf_configure.bazelrc of tensorflow1.13
build --action_env CUDA_TOOLKIT_PATH="/usr/local/cuda"
build --action_env TF_CUDA_COMPUTE_CAPABILITIES="3.5,7.0"
build --action_env LD_LIBRARY_PATH="/usr/local/nvidia/lib:/usr/local/nvidia/lib64"
build --action_env GCC_HOST_COMPILER_PATH="/usr/bin/gcc"

# config cuda
build --config=cuda
build:opt --copt=-march=native
build:opt --copt=-Wno-sign-compare
build:opt --host_copt=-march=native
build:opt --define with_default_optimizations=true
"""
      bazel_rc.write(cuda_config)
  except OSError:
    print("ERROR: Writing .bazelrc")
    sys.exit(1)


def write_config():
  """Retrive compile and link information from tensorflow and write to .bazelrc."""

  cflags = tf.sysconfig.get_compile_flags()

  inc_regex = re.compile("^-I")
  opt_regex = re.compile("^-D")

  include_list = []
  define_list = {"version": "tensorflow_version=115"}
  # set avx instructions by default, skip in ci
  if os.getenv("ONLY_CI", None):
    opt_list = ["-msse4.2"]
    print("only ci, no needs to set march")
  else:
    opt_list = [
        "-msse4.2", "-msse4.1", "-mavx", "-mavx2", "-mfma", "-mfpmath=both"
    ]
  opt_list.append("-frecord-gcc-switches")
  for arg in cflags:
    if inc_regex.match(arg):
      include_list.append(arg)
    elif opt_regex.match(arg):
      opt_list.append(arg)
    else:
      print("WARNING: Unexpected cflag item {}".format(arg))

  if "--use_origin_tf" in sys.argv:
    opt_list.append("-DUSE_ORIGIN_TF")

  if "--xdl" in sys.argv:
    opt_list.append("-Dprotobuf=protobuf3")
    define_list["version"] = "tensorflow_version=112"

  if "--eflops" in sys.argv:
    # eflops not support string key type for kvvariable
    # this macro in tfplus/kv_variable/kernels/kv_variable_ops.cc
    opt_list.append("-DEFLOPS_MODE=1")

  if len(include_list) != 1:
    print("ERROR: Expected a single include directory in " +
          "tf.sysconfig.get_compile_flags()")
    sys.exit(1)

  library_regex = re.compile("^-l")
  libdir_regex = re.compile("^-L")

  library_list = []
  libdir_list = []

  lib = tf.sysconfig.get_link_flags()

  for arg in lib:
    if library_regex.match(arg):
      library_list.append(arg)
    elif libdir_regex.match(arg):
      libdir_list.append(arg)
    else:
      print("WARNING: Unexpected link flag item {}".format(arg))

  if len(library_list) != 1 or len(libdir_list) != 1:
    print("ERROR: Expected exactly one lib and one libdir in" +
          "tf.sysconfig.get_link_flags()")
    sys.exit(1)

  try:

    with open(".bazelrc", "w") as bazel_rc:
      # link dl for traceback
      bazel_rc.write('build --linkopt="-ldl"\n')
      for opt in opt_list:
        bazel_rc.write('build --copt="{}"\n'.format(opt))
      for _, value in define_list.items():
        bazel_rc.write(f'build --define {value}\n')
        print(f"{Bcolors.FAIL}Add global define: {value}{Bcolors.ENDC}")
      bazel_rc.write('build --action_env TF_HEADER_DIR="{}"\n'.format(
          include_list[0][2:]))

      bazel_rc.write("test --cache_test_results=no\n")
      bazel_rc.write("test --test_output all\n")
      bazel_rc.write('build --action_env TF_SHARED_LIBRARY_DIR="{}"\n'.format(
          libdir_list[0][2:]))
      library_name = library_list[0][2:]
      if library_name.startswith(":"):
        library_name = library_name[1:]
      else:
        library_name = "lib" + library_name + ".so"
      bazel_rc.write('build --action_env TF_SHARED_LIBRARY_NAME="{}"\n'.format(
          library_name))
      bazel_rc.close()
  except OSError:
    print("ERROR: Writing .bazelrc")
    sys.exit(1)


def write_sanitizer():
  """ Append asan config for sanitizers"""
  # pylint: disable=line-too-long
  asan_options = "handle_abort=1:allow_addr2line=true:check_initialization_order=true:strict_init_order=true:detect_odr_violation=1"

  ubsan_options = "halt_on_error=true:print_stacktrace=1"
  try:
    with open(".bazelrc", "a") as bazel_rc:
      bazel_rc.write("\n\n# Basic ASAN/UBSAN that works for gcc\n")
      bazel_rc.write("build:asan --define ENVOY_CONFIG_ASAN=1\n")
      bazel_rc.write("build:asan --copt -fsanitize=address\n")
      bazel_rc.write("build:asan --linkopt -lasan\n")
      bazel_rc.write("build:asan --define tcmalloc=disabled\n")
      bazel_rc.write("build:asan --build_tag_filters=-no_asan\n")
      bazel_rc.write("build:asan --test_tag_filters=-no_asan\n")
      bazel_rc.write("build:asan --define signal_trace=disabled\n")
      bazel_rc.write("build:asan --copt -D__SANITIZE_ADDRESS__\n")
      bazel_rc.write(
          'build:asan --test_env=ASAN_OPTIONS="{}"\n'.format(asan_options))
      bazel_rc.write(
          'build:asan --test_env=UBSAN_OPTIONS="{}"\n'.format(ubsan_options))
      bazel_rc.write("build:asan --test_env=ASAN_SYMBOLIZER_PATH\n")
      bazel_rc.write(
          f'build --define=ABSOLUTE_JAVABASE={os.getenv("JAVA_HOME", "/opt/taobao/java/")}\n'
      )
      bazel_rc.write(
          "build --javabase=@bazel_tools//tools/jdk:absolute_javabase\n")
      bazel_rc.close()
  except OSError:
    print("ERROR: Writing .bazelrc")
    sys.exit(1)



print((f"{Bcolors.FAIL}java home is)"
       f": {os.getenv('JAVA_HOME', '/opt/taobao/java')}"
       f"(set by JAVA_HOME[defaults to /opt/taobao/java]){Bcolors.ENDC}"))
write_config()
write_sanitizer()


if "--eflops" in sys.argv:
  print(f"{Bcolors.FAIL}Build with CUDA{Bcolors.ENDC}")
  write_cuda()
else:
  with open(".bazelrc", "a") as bazel_rc:
    bazel_rc.write("build --build_tag_filters=-gpu")
