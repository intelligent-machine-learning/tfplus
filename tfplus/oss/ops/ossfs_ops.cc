// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#include "tensorflow/core/platform/env.h"

#include "tfplus/oss/kernels/ossfs/oss_file_system.h"

namespace tensorflow {

REGISTER_FILE_SYSTEM("oss", OSSFileSystem);

}  // namespace tensorflow
