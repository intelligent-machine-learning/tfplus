// Copyright 2022 The TF-plus Authors. All Rights Reserved.

#include "tensorflow/core/platform/env.h"

#include "tfplus/dfs/kernels/dfs/dfs_file_system.h"

namespace tensorflow {

REGISTER_FILE_SYSTEM("dfs", DfsFileSystem);

}  // namespace tensorflow
