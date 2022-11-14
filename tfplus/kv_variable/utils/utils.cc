// Copyright 2021 The TF-plus Authors. All Rights Reserved.

#include <stdlib.h>
#include "tfplus/kv_variable/utils/utils.h"

namespace tfplus {

  GlobalConfigs gConf;
  __attribute__((constructor)) void ConfigInit() {
    if (gConf.init_done) return;

    auto env = ::getenv("TFPLUS_INFERENCE_ONLY");
    if (env && env[0] == '1')
      gConf.inference_only = true;

    gConf.init_done = true;
  }
}  // namespace tfplus
