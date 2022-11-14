// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#ifndef TFPLUS_PANGU_KERNELS_PANGUFS_PANGU_FILE_SYSTEM_H_
#define TFPLUS_PANGU_KERNELS_PANGUFS_PANGU_FILE_SYSTEM_H_

#include <memory>
#include <string>
#include <vector>

#include "tensorflow/core/platform/env.h"

extern "C" {
struct pangu_internal;
typedef pangu_internal* panguFS;
};

namespace tensorflow {

class LibPangu;

class PanguFileSystem : public FileSystem {
 public:
  PanguFileSystem();
  ~PanguFileSystem();

  Status NewRandomAccessFile(
      const string& fname, std::unique_ptr<RandomAccessFile>* result) override;

  Status NewWritableFile(const string& fname,
                         std::unique_ptr<WritableFile>* result) override;

  Status NewAppendableFile(const string& fname,
                           std::unique_ptr<WritableFile>* result) override;

  Status NewReadOnlyMemoryRegionFromFile(
      const string& fname,
      std::unique_ptr<ReadOnlyMemoryRegion>* result) override;

  Status FileExists(const string& fname) override;

  Status GetChildren(const string& dir, std::vector<string>* result) override;

  Status GetMatchingPaths(const string& pattern,
                          std::vector<string>* results) override;

  Status DeleteFile(const string& fname) override;

  Status CreateDir(const string& name) override;

  Status DeleteDir(const string& name) override;

  Status DeleteRecursively(const string& dirname, int64* undeleted_files,
                           int64* undeleted_dirs) override;

  Status GetFileSize(const string& fname, uint64* size) override;

  Status RenameFile(const string& src, const string& target) override;

  Status Stat(const string& fname, FileStatistics* stat) override;

  string TranslateName(const string& name) const override;

 private:
  Status NewWritableFileInternal(const string& fname,
                                 std::unique_ptr<WritableFile>* result,
                                 int overwrite);

  Status Connect(const string& fname);

  LibPangu* pangu_;
};
}  // namespace tensorflow

#endif  // TFPLUS_PANGU_KERNELS_PANGUFS_PANGU_FILE_SYSTEM_H_
