// Copyright 2020 The TF-plus Authors. All Rights Reserved.

#ifndef TFPLUS_KV_VARIABLE_KERNELS_UTILITY_H_
#define TFPLUS_KV_VARIABLE_KERNELS_UTILITY_H_
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <cctype>

#include "farmhash.h"  // NOLINT(build/include_subdir)
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/resource_mgr.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/framework/tensor_types.h"
#include "tensorflow/core/framework/types.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/file_system.h"
#include "tensorflow/core/platform/types.h"
#include "tfplus/kv_variable/kernels/tensor_bundle.h"

namespace tfplus {
using ::tensorflow::uint16;

// Get current unix time, by default divided by 3600*24 as days.
uint16 GetCurrentUnixTimeByDivisor(uint64_t divisor = 3600 * 24);

uint16_t GetUint16FromUint32(const uint32_t& source, bool isLow16Bits);

uint32_t MakeUint32FromUint16(uint16_t high, uint16_t low);

// Saturate arithmetic operation.
inline uint16_t SaturateMaxFrequency(const int32_t freq) {
  return std::min<int32_t>(freq, std::numeric_limits<uint16_t>::max());
}

// Saturate arithmetic addition operation.
inline uint16_t SaturateAddFrequency(const uint16_t val, const uint16_t delta) {
  uint16_t new_val = val + delta;
  if (new_val < val)  /* Can only happen due to overflow */
    new_val = -1;
  return new_val;
}

template <typename T>
inline T StringToValue(const char* value, T default_if_failed) {
  std::istringstream ss(value);
  T res;
  ss >> res;
  if (ss.fail()) {
    return default_if_failed;
  }
  return res;
}

template <typename T>
inline T GetEnvVar(const char* key, T val) {
  char* name = getenv(key);
  if (name != nullptr && strlen(name) > 0) {
    return StringToValue<T>(name, val);
  } else {
    return val;
  }
}

template <typename TL>
Status AddStringTensorImpl(const string& key, const TL& strings,
                           TensorShape shape, BundleWriter* writer) {
  return ::tensorflow::errors::Internal(
      "AddStringTensorImpl function is unimplemented.");
}

template <>
inline Status AddStringTensorImpl(const string& key,
                                  const std::vector<string*>& strings,
                                  TensorShape shape, BundleWriter* writer) {
  LOG(INFO) << "AddStringTensor: " << key << " size: " << strings.size();
  return writer->AddStringTensor(key, strings, shape);
}

template <typename T>
struct google_floor_mod {
  const inline T operator()(const T& x,
                     const int& y) const {
    T trunc_mod = x % y;
    return (x < T(0)) == (y < T(0)) ? trunc_mod : (trunc_mod + y) % y;
  }
};

template <typename TL>
inline int ModKeyImpl(const TL& key, const int& num_shards) {
  google_floor_mod<TL> floor_mod;
  return floor_mod(key, num_shards);
}

template <>
inline int ModKeyImpl(const string& key, const int& num_shards) {
  return ::util::Fingerprint64<string>(key) % num_shards;
}

// A restore operation for a single tensor.  Small tensors may be restored
// directly from the op thread to improve read locality.  Large tensors can be
// restored from a thread pool: this requires creating a separate BundleReader
// for each restore.
struct RestoreOp {
  RestoreOp& operator=(const RestoreOp&) = delete;
  RestoreOp(OpKernelContext* ctx, std::string tensor_name,
            std::string reader_prefix, Tensor* restored_tensor)
      : ctx(ctx),
        tensor_name(tensor_name),
        reader_prefix(reader_prefix),
        restored_tensor(restored_tensor) {}
  bool should_run_in_pool(BundleReader* reader) const {
    TensorShape restored_full_shape;

    // Ignore status here; we'll catch the error later.
    if (!reader->LookupTensorShape(tensor_name, &restored_full_shape).ok()) {
      return false;
    }

    return restored_full_shape.num_elements() > kLargeShapeThreshold;
  }

  void run_with_new_reader(const std::vector<int64>& indices) {
    BundleReader reader(Env::Default(), reader_prefix);
    if (!reader.status().ok()) {
      status = reader.status();
      return;
    }
    if (!indices.empty()) {
      status = run_with_indices(&reader, indices);
    } else {
      status = run(&reader);
    }
  }

  Status run_with_indices(BundleReader* reader,
                          const std::vector<int64>& indices) {
    TensorShape restored_full_shape;
    DataType restored_dtype;
    TF_RETURN_IF_ERROR(reader->LookupDtypeAndShape(tensor_name, &restored_dtype,
                                                   &restored_full_shape));
    if (!indices.empty()) {
      restored_full_shape.set_dim(0, indices.size());
    }

    VLOG(1) << "Restoring tensor: " << tensor_name << " : "
            << restored_full_shape.num_elements();
    TF_RETURN_IF_ERROR(ctx->allocate_temp(restored_dtype, restored_full_shape,
                                          restored_tensor));
    // Lookup the full tensor.
    if (indices.empty()) {
      TF_RETURN_IF_ERROR(reader->Lookup(tensor_name, restored_tensor));
    } else {
      TF_RETURN_IF_ERROR(
          reader->LookupWithIndices(tensor_name, restored_tensor, indices));
    }
    return Status::OK();
  }

  Status run(BundleReader* reader) {
    TensorShape restored_full_shape;
    DataType restored_dtype;
    TF_RETURN_IF_ERROR(reader->LookupDtypeAndShape(tensor_name, &restored_dtype,
                                                   &restored_full_shape));
    VLOG(1) << "Restoring tensor: " << tensor_name << " : "
            << restored_full_shape.num_elements();
    TF_RETURN_IF_ERROR(ctx->allocate_temp(restored_dtype, restored_full_shape,
                                          restored_tensor));
    // Lookup the full tensor.
    TF_RETURN_IF_ERROR(reader->Lookup(tensor_name, restored_tensor));
    return Status::OK();
  }

  OpKernelContext* ctx;
  std::string tensor_name;
  std::string reader_prefix;
  Tensor* restored_tensor;
  // Tensors larger than this threshold will be restored from a thread-pool.
  const int64 kLargeShapeThreshold = 16 << 20;  // 16M
  ::tensorflow::Status status;
};

std::string GetLegalPhstoreTableName(const std::string& s);

Status CopyFiles(Env* env, const std::vector<std::string>& src_filenames,
                 const std::string& dest_dir,
                 std::vector<std::string>* dst_filenames);

Status CreateTarFile(const char* tarname,
                     const std::vector<std::string>* filename);

Status ExtractTarFileToDirectory(const std::string& tar_file,
                                 const std::string& directory_path);

void DeleteDirectory(const std::string& directory);

std::string RemoveCheckpointPathTempSuffix(const std::string& path);

Status GetChildren(const string& dir, std::vector<string>* result);

Status GetChildrenWithParentDir(const string& dir, std::vector<string>* result);

std::string GenerateSnapshotPath(const std::string& prefix);

std::string GenerateTarFileName(const std::string& table_name);

std::string JoinPath(const std::string& dir, const std::string& name);

std::string GetShortFileName(const std::string& filename);

std::string RemoveCheckpointPathTempSuffix(const std::string& path);

std::string ConcatStringList(const std::vector<std::string>& string_list);

bool CanMakeSymlink(const std::vector<std::string>& pathnames,
                    const std::string& dest_dir);

Status SymlinkFiles(Env* env, const std::vector<std::string>& files,
                    const std::string& dst_path,
                    std::vector<std::string>* dst_filenames);

Status Unlink(const std::string& path);
}  // namespace tfplus

#endif  // TFPLUS_KV_VARIABLE_KERNELS_UTILITY_H_
