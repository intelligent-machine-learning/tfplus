// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#include "tensorflow/core/platform/env.h"

#include "tfplus/pangu/kernels/pangufs/pangu_file_system.h"

namespace tensorflow {

REGISTER_FILE_SYSTEM("pangu", PanguFileSystem);

}  // namespace tensorflow
