// Copyright 2021 The TF-plus Authors. All Rights Reserved.

#ifndef TFPLUS_KV_VARIABLE_UTILS_UTILS_H_
#define TFPLUS_KV_VARIABLE_UTILS_UTILS_H_

namespace tfplus {
struct GlobalConfigs {
  bool init_done = false;
  bool inference_only = false;
};

}  // namespace tfplus

#endif  // TFPLUS_KV_VARIABLE_UTILS_UTILS_H_
