# Copyright 2019 The TF-plus Authors. All Rights Reserved.
"""
find and tar so
"""

import fnmatch
import os
import pkgutil
import shutil
import sys
import tarfile
import tensorflow as tf


def get_suffix():
  if "pai" in tf.__version__.lower():
    if tf.test.is_built_with_cuda():
      return ".eflops"
    if pkgutil.find_loader('xdl') is not None:
      return ".xdl"
    return ".pai"
  return ""

suffix = get_suffix()

datapath = "bazel-bin"
output_so = sys.argv[1]
SPEICAL_SO = {
    # pylint: disable=line-too-long
    "bazel-bin/external/com_antfin_libzdfs/build/libzdfs.so": "bazel-bin/tfplus/dfs/python/ops/libzdfs.so"
}

with tarfile.open(output_so, "w:gz") as tar:
  # copy normal so
  for rootname, _, filenames in os.walk(os.path.join(datapath, "tfplus")):
    if (not fnmatch.fnmatch(rootname, "*test*")
        and not fnmatch.fnmatch(rootname, "*runfiles*")):
      for filename in fnmatch.filter(filenames, "*.so"):
        src = os.path.join(rootname, filename)
        dst = src
        if suffix:
          dst = src + suffix
          shutil.copyfile(src, dst)
        tar.add(dst)
  # copy special so
  for src, dst in SPEICAL_SO.items():
    if suffix:
      dst = dst + suffix
      shutil.copyfile(src, dst)
    tar.add(dst)
