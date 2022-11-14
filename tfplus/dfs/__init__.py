# Copyright 2019 The TF-plus Authors. All Rights Reserved.
"""Alibaba Distributed File System.

@@dfs_ops
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from tensorflow.python.util.all_util import remove_undocumented

from tfplus.dfs.python.ops import dfs_ops  # pylint: disable=unused-import

_allowed_symbols = [
    "dfs_ops",
]

remove_undocumented(__name__, allowed_exception_list=_allowed_symbols)
