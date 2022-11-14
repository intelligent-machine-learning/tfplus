# Copyright 2019 The TF-plus Authors. All Rights Reserved.

"""Pangu File System Support

Pangu is an Distributed FileSystem provided by Alibaba. This module
implements a filesystem on top of it.
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function


from tfplus.common import _load_library
_load_library("_pangu_ops.so")
