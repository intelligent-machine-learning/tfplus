// Copyright 2022 The TF-plus Authors. All Rights Reserved.

#ifndef TFPLUS_DFS_KERNELS_DFS_DFS_FILE_SYSTEM_H_
#define TFPLUS_DFS_KERNELS_DFS_DFS_FILE_SYSTEM_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tensorflow/core/lib/gtl/map_util.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/mutex.h"
#include "zdfs/zdfs.h"

namespace tensorflow {

class DfsFileSystem : public FileSystem {
 public:
  DfsFileSystem();
  ~DfsFileSystem();

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

 private:
  Status NewWritableFileInternal(const string& fname,
                                 std::unique_ptr<WritableFile>* result,
                                 bool overwrite);

  Status GetConnection(const string& fna,
                       std::shared_ptr<zdfs::PanguFileSystem>* pangu);

  mutex mu_;
  std::unordered_map<string, std::shared_ptr<zdfs::PanguFileSystem>>
      dfs_connections_ GUARDED_BY(mu_);
};
}  // namespace tensorflow

#endif  // TFPLUS_DFS_KERNELS_DFS_DFS_FILE_SYSTEM_H_
