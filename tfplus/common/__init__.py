# Copyright 2022 The TF-plus Authors. All Rights Reserved.
"""tfplus common"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import ctypes as ct
import os
import sys
import inspect
import pkgutil
from distutils.version import LooseVersion
import tensorflow as tf
from tensorflow.python.platform import tf_logging as logging
from tensorflow import errors


def _load_library(filename, lib="op", load_fn=None):
  """_load_library"""
  f = inspect.getfile(sys._getframe(1))  # pylint: disable=protected-access

  # Construct filename
  f = os.path.join(os.path.dirname(f), filename)
  suffix = get_suffix()
  if os.path.exists(f + suffix):
    f = f + suffix
  filenames = [f]

  # Add datapath to load if en var is set, used for running tests where shared
  # libraries are built in a different path
  datapath = os.environ.get('TFPLUS_DATAPATH')
  if datapath is not None:
    # Build filename from `datapath` + `package_name` + `relpath_to_library`
    f = os.path.join(datapath, os.path.relpath(f, os.path.dirname(filename)))
    suffix = get_suffix()
    if os.path.exists(f + suffix):
      f = f + suffix
    filenames.append(f)

  # Function to load the library, return True if file system library is loaded
  load_fn = load_fn or (tf.load_op_library if lib == "op" \
      else lambda f: tf.compat.v1.load_file_system_library(f) is None)

  # Try to load all paths for file, fail if none succeed
  errs = []
  for f in filenames:
    try:
      l = load_fn(f)
      if l is not None:
        return l
      # if load_fn is tf.load_library:
      #   return
    except errors.NotFoundError as e:
      errs.append(str(e))
  raise NotImplementedError(
      "unable to open file: " +
      "{}, from paths: {}\ncaused by: {}".format(filename, filenames, errs)) # pylint: disable=consider-using-f-string


def is_tf_1_13_or_higher():
  if LooseVersion(tf.__version__) >= '1.13.0':
    return True
  return False

def is_pai_tf():
  return "pai" in tf.__version__.lower()

def get_suffix():
  """helper to get suffix of shared object"""
  suffix = os.getenv("SO_SUFFIX", None)
  if suffix:
    return suffix
  if "pai" in tf.__version__.lower():
    if tf.test.is_built_with_cuda():
      return ".eflops"
    if pkgutil.find_loader('xdl') is not None:
      return ".xdl"
    return ".pai"
  return ""

def ctypes_load_library(path):
  ct.cdll.LoadLibrary(path)
  logging.info("`%s` is loaded.", path)
