# Copyright 2022 The TF-plus Authors. All Rights Reserved.
"""utils to use KvVariable"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function


def get_kv_variable_op_types():
  return ("KvVariable", "KvVariableV3")


def is_kv_variable_op_type(op_type):
  return op_type in get_kv_variable_op_types()
