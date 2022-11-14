// Copyright 2022 The TF-plus Authors. All Rights Reserved.

#include "tfplus/dfs/kernels/dfs/dfs_file_system.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <utility>

#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/lib/strings/strcat.h"
#include "tensorflow/core/platform/default/logging.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/file_system.h"
#include "tensorflow/core/platform/file_system_helper.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/mutex.h"
#include "tensorflow/core/platform/posix/error.h"

using mutex_write_lock = ::tensorflow::mutex_lock;

namespace tensorflow {

static const std::map<int, std::string> dfsErrorMessageMap = {
    {0, "Success"},
    {1, "Operation not permitted"},
    {2, "No such file or directory"},
    {3, "I/O error"},
    {4, "Try again"},
    {5, "Permission denied"},
    {6, "Resource busy"},
    {7, "File exists"},
    {8, "Is a directory"},
    {9, "Invalid argument"},
    {10, "No space left on device"},
    {11, "Read-only file system"},
    {12, "Too many links"},
    {13, "Function not implemented"},
    {14, "Bad file handle"},
    {15, "Timed out"},
    {16, "Operation already in progress"},
    {17, "Quota exceeded"},
    {18, "File handle in bad state"},
    {19, "Bad address"}};

const std::string& DfsErrorMessage(const int error_code) {
  return tensorflow::gtl::FindWithDefault(
      dfsErrorMessageMap, error_code,
      "Unknown error code " + std::to_string(error_code));
}

DfsFileSystem::DfsFileSystem() {
  zdfs::PanguOptions options;
  options.log_level = zdfs::LogLevel::ERROR;
  zdfs::PanguFileSystem::SetOptions(options);
}

DfsFileSystem::~DfsFileSystem() {}

Status ParseDfsPath(const string& fname, string* cluster, string* file_path) {
  StringPiece scheme, host, path;
  io::ParseURI(fname, &scheme, &host, &path);

  if (scheme != "dfs") {
    return errors::InvalidArgument("Dfs path does not start with 'dfs://':",
                                   fname);
  }

  if (host.empty() || path.empty()) {
    return errors::InvalidArgument("cluster or file_path can not be empty for ",
                                   fname);
  }

  *cluster = "dfs://" + std::string(host);
  *file_path = std::string(path);
  return Status::OK();
}

Status OpenFile(std::shared_ptr<zdfs::PanguFileSystem> pangu,
                const std::string& fname, zdfs::OpenMode mode,
                std::shared_ptr<zdfs::PanguFile>* pangu_file) {
  DCHECK(pangu != nullptr);
  std::shared_ptr<zdfs::PanguFile> file;
  std::error_code ec = pangu->OpenFile(fname, mode, {}, &file, nullptr);
  if (ec) {
    if (ec.value() == static_cast<int>(zdfs::PanguErrorCode::PANGU_ENOENT)) {
      return errors::NotFound("failed to open file ", fname,
                              " cause not found");
    }

    return errors::Internal("failed to open file ", fname,
                            " error: ", DfsErrorMessage(ec.value()));
  }
  if (!file) {
    return errors::Internal("failed to open file ", fname);
  }

  *pangu_file = file;
  return Status::OK();
}

Status CloseFile(std::shared_ptr<zdfs::PanguFile> pangu_file,
                 const std::string& filename) {
  DCHECK(pangu_file != nullptr);
  std::error_code ec = pangu_file->Close({}, nullptr);
  if (ec) {
    return errors::Internal("failed to close ", filename,
                            ", error: ", DfsErrorMessage(ec.value()));
  }
  return Status::OK();
}

class DfsRandomAccessFile : public RandomAccessFile {
 public:
  DfsRandomAccessFile(const std::string& fname,
                      std::shared_ptr<zdfs::PanguFile> pangu_file,
                      std::shared_ptr<zdfs::PanguFileSystem> pangu)
      : filename_(fname) {
    pangu_ = pangu;
    pangu_file_ = pangu_file;
  }

  ~DfsRandomAccessFile() override {
    if (pangu_file_ != nullptr) {
      CloseFile(pangu_file_, filename_);
      pangu_file_.reset();
    }
  }

  bool IsValid() {
    return pangu_file_ != nullptr && pangu_file_.get() != nullptr;
  }

  Status ReadInternal(uint64_t offset, size_t length, void* buffer,
                      uint64_t* read_bytes) const {
    std::error_code ec =
        pangu_file_->PRead(offset, length, {}, buffer, read_bytes, nullptr);
    if (ec) {
      return errors::Internal("failed to read ", filename_,
                              ", offset: ", offset, " length: ", length,
                              " error: ", DfsErrorMessage(ec.value()));
    }
    return Status::OK();
  }

  // random access, read data from specified offset in file
  Status Read(uint64 offset, size_t n, StringPiece* result,
              char* scratch) const override {
    Status s;
    static int buffer_size = 1024 * 1024;  // 1 MB
    char* dst = scratch;
    bool eof_retried = false;
    while (n > 0 && s.ok()) {
      int to_read = n > static_cast<size_t>(buffer_size) ? buffer_size
                                                         : static_cast<int>(n);
      mutex_lock lock(mu_);
      uint64_t bytes_read;
      TF_RETURN_IF_ERROR(ReadInternal(offset, to_read, dst, &bytes_read));
      if (bytes_read > 0) {
        dst += bytes_read;
        n -= bytes_read;
        offset += bytes_read;
      } else if (!eof_retried) {
        TF_RETURN_IF_ERROR(CloseFile(pangu_file_, filename_));
        TF_RETURN_IF_ERROR(OpenFile(pangu_, filename_,
                                    zdfs::OpenMode::READ_ONLY, &pangu_file_));
        eof_retried = true;
      } else if (eof_retried) {
        s = Status(error::OUT_OF_RANGE, "Read less bytes than requested");
      }
    }
    *result = StringPiece(scratch, dst - scratch);
    return s;
  }

 private:
  const string filename_;

  mutable mutex mu_;
  mutable std::shared_ptr<zdfs::PanguFile> pangu_file_ GUARDED_BY(mu_);
  std::shared_ptr<zdfs::PanguFileSystem> pangu_;
};

Status DfsFileSystem::GetConnection(
    const string& cluster, std::shared_ptr<zdfs::PanguFileSystem>* pangu) {
  mutex_write_lock lock(mu_);
  auto connection = tensorflow::gtl::FindOrNull(dfs_connections_, cluster);
  if (connection != nullptr) {
    *pangu = *connection;
  } else {
    std::shared_ptr<zdfs::PanguFileSystem> new_pangu =
        zdfs::PanguFileSystem::Create(cluster);
    if (new_pangu.get() == nullptr) {
      return errors::Unavailable("failed to create zdfs::PanguFileSystem to ",
                                 cluster);
    }
    /*
    zdfs::PanguOptions options;
    options.log_level = zdfs::LogLevel::INFO;
    zdfs::PanguFileSystem::SetOptions(options);
    */
    *pangu = new_pangu;
    dfs_connections_.insert(std::make_pair(cluster, new_pangu));
  }

  return Status::OK();
}

Status DfsFileSystem::NewRandomAccessFile(
    const string& ori_fname, std::unique_ptr<RandomAccessFile>* result) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  std::string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(ori_fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));

  std::shared_ptr<zdfs::PanguFile> pangu_file;
  TF_RETURN_IF_ERROR(
      OpenFile(pangu, file_path, zdfs::OpenMode::READ_ONLY, &pangu_file));
  result->reset(new DfsRandomAccessFile(file_path, pangu_file, pangu));
  return Status::OK();
}

class DfsWritableFile : public WritableFile {
 public:
  DfsWritableFile(const string& fname,
                  std::shared_ptr<zdfs::PanguFile> pangu_file)
      : filename_(fname) {
    pangu_file_ = pangu_file;
  }

  ~DfsWritableFile() override {
    if (pangu_file_ != nullptr) {
      CloseFile(pangu_file_, filename_);
      pangu_file_.reset();
    }
  }

  Status AppendInternal(const void* buffer, size_t length) {
    uint64_t offset;
    std::error_code ec =
        pangu_file_->Append(buffer, length, {}, &offset, nullptr);
    if (ec) {
      return errors::Internal("failed to append to ", filename_,
                              " , error: ", DfsErrorMessage(ec.value()));
    }

    return Status::OK();
  }

  Status Append(StringPiece data) override {
    const char* src = data.data();
    size_t size = data.size();
    static int buffer_size = 1024 * 1024;  // 1MB
    while (size > 0) {
      int to_write = size > static_cast<size_t>(buffer_size)
                         ? buffer_size
                         : static_cast<int>(size);
      TF_RETURN_IF_ERROR(AppendInternal(src, to_write));
      size -= to_write;
      src += to_write;
    }
    return Status::OK();
  }

  Status Flush() override { return Sync(); }

  Status Sync() override {
    size_t length;
    std::error_code ec = pangu_file_->Flush({}, &length, nullptr);
    if (ec) {
      return errors::Internal("failed to flush ", filename_,
                              " error: ", DfsErrorMessage(ec.value()));
    }

    return Status::OK();
  }

  Status Close() override { return CloseFile(pangu_file_, filename_); }

 private:
  const string filename_;
  std::shared_ptr<zdfs::PanguFile> pangu_file_;
};

Status DfsFileSystem::NewWritableFileInternal(
    const string& ori_fname, std::unique_ptr<WritableFile>* result,
    bool overwrite) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  std::string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(ori_fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));
  if (overwrite && FileExists(ori_fname).ok()) {
    TF_RETURN_IF_ERROR(DeleteFile(ori_fname));
  }

  std::error_code ec = pangu->CreateFile(file_path, {}, nullptr);
  if (ec) {
    return errors::Internal("failed to create ", file_path,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  std::shared_ptr<zdfs::PanguFile> pangu_file;
  TF_RETURN_IF_ERROR(
      OpenFile(pangu, file_path, zdfs::OpenMode::WRITE_ONLY, &pangu_file));

  result->reset(new DfsWritableFile(file_path, pangu_file));
  return Status::OK();
}

Status DfsFileSystem::NewWritableFile(const string& fname,
                                      std::unique_ptr<WritableFile>* result) {
  // force overwrite
  return NewWritableFileInternal(fname, result, true);
}

Status DfsFileSystem::NewAppendableFile(const string& fname,
                                        std::unique_ptr<WritableFile>* result) {
  return NewWritableFileInternal(fname, result, false);
}

Status DfsFileSystem::NewReadOnlyMemoryRegionFromFile(
    const string& fname, std::unique_ptr<ReadOnlyMemoryRegion>* result) {
  return errors::Unimplemented("dfs does not support ReadOnlyMemoryRegion");
}

void FixDirectoryName(std::string* name) {
  CHECK(name != nullptr);
  int len = name->length();
  if (len && name->at(len - 1) != '/') {
    name->push_back('/');
  }
}

void RemoveDirectorySuffix(std::string* name) {
  CHECK(name != nullptr);
  int len = name->length();
  if (len && name->at(len - 1) == '/') {
    name->erase(len - 1, 1);
  }
}

Status DfsFileSystem::GetChildren(const std::string& fname,
                                  std::vector<std::string>* result) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  std::string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));
  FixDirectoryName(&file_path);

  std::vector<std::string> entries;
  std::error_code ec =
      pangu->ListDirectory(file_path, {}, &entries, nullptr, nullptr, nullptr);

  if (ec) {
    if (ec.value() == static_cast<int>(zdfs::PanguErrorCode::PANGU_ENOENT)) {
      return errors::NotFound(fname, " not found");
    }

    return errors::Internal("failed to list directory ", fname,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  result->clear();
  for (auto entrie : entries) {
    RemoveDirectorySuffix(&entrie);
    result->push_back(std::string(io::Basename(entrie)));
  }
  return Status::OK();
}

Status DfsFileSystem::GetMatchingPaths(const string& pattern,
                                       std::vector<string>* results) {
  return internal::GetMatchingPaths(this, Env::Default(), pattern, results);
}

Status DfsFileSystem::DeleteFile(const string& fname) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  std::string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));

  std::error_code ec = pangu->Delete(file_path, {}, nullptr);

  if (ec) {
    if (ec.value() == static_cast<int>(zdfs::PanguErrorCode::PANGU_ENOENT)) {
      return Status::OK();
    }
    return errors::Internal("failed to delete file ", fname,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  return Status::OK();
}

Status DfsFileSystem::CreateDir(const string& fname) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  std::string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));
  FixDirectoryName(&file_path);

  std::error_code ec = pangu->CreateDirectory(file_path, {}, nullptr);
  if (ec) {
    return errors::Internal("failed to create directory ", fname,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  return Status::OK();
}

Status DfsFileSystem::DeleteDir(const std::string& fname) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  std::string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));

  FixDirectoryName(&file_path);
  std::error_code ec = pangu->Delete(file_path, {}, nullptr);

  if (ec) {
    if (ec.value() == static_cast<int>(zdfs::PanguErrorCode::PANGU_ENOENT)) {
      return Status::OK();
    }
    return errors::Internal("failed to delete directory ", fname,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  return Status::OK();
}

Status DfsFileSystem::DeleteRecursively(const string& dirname,
                                        int64* undeleted_files,
                                        int64* undeleted_dirs) {
  *undeleted_files = 0;
  *undeleted_dirs = 0;
  return DeleteDir(dirname);
}

Status DfsFileSystem::Stat(const std::string& fname, FileStatistics* stats) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  string cluster, file_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(fname, &cluster, &file_path));
  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));

  zdfs::EntryStat stat;
  std::error_code ec = pangu->Stat(file_path, {}, &stat, nullptr);
  if (ec) {
    if (ec.value() == static_cast<int>(zdfs::PanguErrorCode::PANGU_ENOENT)) {
      return errors::NotFound(fname, " not found");
    }

    return errors::Internal("failed to stat ", fname,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  stats->is_directory = stat.IsDir();
  if (stat.IsDir()) {
    stats->length = 0;
    stats->mtime_nsec = 0;
  } else {
    stats->length = static_cast<int64>(stat.file.length);
    stats->mtime_nsec = static_cast<int64>(stat.file.modify_time) * 1e9;
  }
  return Status::OK();
}

Status DfsFileSystem::FileExists(const string& fname) {
  FileStatistics stat;
  if (Stat(fname, &stat).ok()) {
    return Status::OK();
  } else {
    return errors::NotFound(fname, " does not exists");
  }
}

Status DfsFileSystem::GetFileSize(const std::string& fname, uint64* size) {
  FileStatistics stat;
  TF_RETURN_IF_ERROR(Stat(fname, &stat));
  *size = static_cast<uint64>(stat.length);
  return Status::OK();
}

Status DfsFileSystem::RenameFile(const std::string& src,
                                 const std::string& target) {
  std::shared_ptr<zdfs::PanguFileSystem> pangu;
  string cluster, src_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(src, &cluster, &src_path));

  string dst_cluster, dst_path;
  TF_RETURN_IF_ERROR(ParseDfsPath(target, &dst_cluster, &dst_path));

  if (cluster != dst_cluster) {
    return errors::InvalidArgument("failed to rename ", src, " to ", target,
                                   " cause there are not same dfs cluster");
  }

  TF_RETURN_IF_ERROR(GetConnection(cluster, &pangu));

  FileStatistics stat;
  Status s = Stat(target, &stat);
  if (s == Status::OK()) {
    if (stat.is_directory) {
      TF_RETURN_IF_ERROR(DeleteDir(target));
    } else {
      TF_RETURN_IF_ERROR(DeleteFile(target));
    }
  }

  if (IsDirectory(src) == Status::OK()) {
    FixDirectoryName(&src_path);
    FixDirectoryName(&dst_path);
  }

  zdfs::RenameOptions rename_options;
  rename_options.recursive = true;
  std::error_code ec =
      pangu->Rename(src_path, dst_path, rename_options, nullptr);
  if (ec) {
    return errors::Internal("failed to rename ", src, " to ", target,
                            " error: ", DfsErrorMessage(ec.value()));
  }

  return Status::OK();
}
}  // namespace tensorflow
