# Copyright 2019 The TF-plus Authors. All Rights Reserved.

"""OSS File System Support

OSS is an Object Storage Service provided by Alibaba Cloud. This module
implements a filesystem on top of it.
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from tfplus.common import _load_library

_load_library("_oss_ops.so")
