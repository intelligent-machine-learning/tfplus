// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#include "tfplus/pangu/kernels/pangufs/pangu_file_system.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>

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
#ifdef USE_PANGU2
#include "third_party/pangu/pangu2_api.h"
#else
#include "third_party/pangu/pangu_api.h"
#endif

namespace tensorflow {

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
inline T GetEnvOrDefault(const char* key, T val) {
  char* name = getenv(key);
  if (name != nullptr) {
    return StringToValue<T>(name, val);
  } else {
    return val;
  }
}

#define RET_IOERROR_IF_RC_NOT_ZERO(expr, fname)                  \
  do {                                                           \
    int rc = (expr);                                             \
    if (rc != 0) return IOError(#expr " failed, " + fname, -rc); \
  } while (0)

template <typename R, typename... Args>
Status BindFunc(void* handle, const char* name,
                std::function<R(Args...)>* func) {
  void* symbol_ptr = nullptr;
  TF_RETURN_IF_ERROR(
      Env::Default()->GetSymbolFromLibrary(handle, name, &symbol_ptr));
  *func = reinterpret_cast<R (*)(Args...)>(symbol_ptr);
  return Status::OK();
}

class LibPangu {
  class PanguGuard {
   public:
    explicit PanguGuard(LibPangu* pangu) : pangu_(pangu) {
      string cluster_name = GetEnvOrDefault<string>("PANGU_CLUSTER_NAME", "");
      VLOG(1) << "cluster_name:[" << cluster_name << "]";
      int rc = pangu_->pangu_init(("pangu://" + cluster_name).c_str(), 0);
      if (rc != 0) {
        LOG(FATAL) << "pangu cluster name is invalid=[" + cluster_name << "]";
      }
    }

    ~PanguGuard() { pangu_->pangu_uninit(); }

   private:
    LibPangu* pangu_;
  };

 public:
  static LibPangu* Load() {
    static LibPangu* lib = []() -> LibPangu* {
      LibPangu* lib = new LibPangu;
      lib->LoadAndBind();
      return lib;
    }();

    return lib;
  }

  // The status, if any, from failure to load.
  Status status() { return status_; }

  std::function<int(const char*, int)> pangu_init;
  std::function<int()> pangu_uninit;
  std::function<int(file_handle_t)> close_file;
  std::function<int(file_handle_t)> fsync;
  std::function<int64_t(file_handle_t, uint64_t, int, char*)> pread;
  std::function<int(const char*, int, int, file_handle_t*)> open_file;
  std::function<int(file_handle_t, const char*, int)> write;
  std::function<int(const char*, int, int, int)> create;
  std::function<int(const char*, file_status_t*)> get_status;
  std::function<int(const char*, pangu_dir_t*, int)> open_dir;
  std::function<int(pangu_dir_t, char*, int*, file_status_t*)> read_dir;
  std::function<int(pangu_dir_t)> close_dir;
  std::function<int(const char*, int)> remove_file_or_dir;
  std::function<int(const char*, int)> mkdir;
  std::function<int(const char*, int)> rmdir;
  std::function<int(const char*, const char*)> rename_file;
  std::function<int(const char*, const char*)> rename_dir;

#ifdef USE_PANGU2
  std::function<int(const char*, int, int, file_handle_t*)> pangu2_open;
  std::function<int(file_handle_t, char*, int, uint64_t)> pangu2_pread;
  std::function<int(const char*)> pangu2_rmdir1;
  std::function<int(const char*, int, int, const char*, int)> pangu2_create;
#else
  std::function<int(const char*, int, int, const char*, const char*, int, int,
                    int)>
      pangu_create1;

  std::function<int64_t(file_handle_t, int64_t, int)> pangu_lseek;
  std::function<int(const char*, int, int, int, file_handle_t*)> pangu_open;
  std::function<int(file_handle_t, char*, int, int)> pangu_read1;
  std::function<int(file_handle_t, const char*, int, int)> pangu_write1;
#endif

 private:
  void LoadAndBind() {
    auto TryLoadAndBind = [this](const char* name, void** handle) -> Status {
      TF_RETURN_IF_ERROR(Env::Default()->LoadLibrary(name, handle));

#define BIND_PANGU_FUNC_SAME_NAME(function) \
  TF_RETURN_IF_ERROR(BindFunc(*handle, #function, &function));

#define BIND_PANGU_FUNC(lib_function, function) \
  TF_RETURN_IF_ERROR(BindFunc(*handle, #lib_function, &function));

      using std::placeholders::_1;
      using std::placeholders::_2;
      using std::placeholders::_3;
      using std::placeholders::_4;

#ifdef USE_PANGU2
      BIND_PANGU_FUNC(pangu2_init, pangu_init)
      BIND_PANGU_FUNC(pangu2_uninit, pangu_uninit)
      BIND_PANGU_FUNC(pangu2_close, close_file)
      BIND_PANGU_FUNC(pangu2_fsync, fsync)
      BIND_PANGU_FUNC(pangu2_append, write)
      BIND_PANGU_FUNC(pangu2_get_status, get_status)
      BIND_PANGU_FUNC(pangu2_open_dir, open_dir)
      BIND_PANGU_FUNC(pangu2_read_dir, read_dir)
      BIND_PANGU_FUNC(pangu2_close_dir, close_dir)
      BIND_PANGU_FUNC(pangu2_remove, remove_file_or_dir)
      BIND_PANGU_FUNC(pangu2_mkdir, mkdir)
      BIND_PANGU_FUNC(pangu2_rename, rename_file)
      BIND_PANGU_FUNC(pangu2_rename, rename_dir)
      BIND_PANGU_FUNC(pangu2_rmdir1, rmdir)
      BIND_PANGU_FUNC_SAME_NAME(pangu2_open);
      BIND_PANGU_FUNC_SAME_NAME(pangu2_pread);
      BIND_PANGU_FUNC_SAME_NAME(pangu2_rmdir1);
      BIND_PANGU_FUNC_SAME_NAME(pangu2_create);

      pread = std::bind(pangu2_pread, _1, _4, _3, _2);
      open_file = std::bind(pangu2_open, _1, _2, _3, _4);
      create = std::bind(pangu2_create, _1, _2, _3, "BIGFILE_APPNAME", _4);
#else
      BIND_PANGU_FUNC(pangu_close, close_file)
      BIND_PANGU_FUNC(pangu_fsync, fsync)
      BIND_PANGU_FUNC(pangu_get_status, get_status)
      BIND_PANGU_FUNC(pangu_open_dir, open_dir)
      BIND_PANGU_FUNC(pangu_read_dir, read_dir)
      BIND_PANGU_FUNC(pangu_close_dir, close_dir)
      BIND_PANGU_FUNC(pangu_remove, remove_file_or_dir)
      BIND_PANGU_FUNC(pangu_mkdir, mkdir)
      BIND_PANGU_FUNC(pangu_rmdir, rmdir)
      BIND_PANGU_FUNC(pangu_rename_file, rename_file)
      BIND_PANGU_FUNC(pangu_rename_dir, rename_dir)

      BIND_PANGU_FUNC_SAME_NAME(pangu_create1)
      BIND_PANGU_FUNC_SAME_NAME(pangu_init)
      BIND_PANGU_FUNC_SAME_NAME(pangu_lseek)
      BIND_PANGU_FUNC_SAME_NAME(pangu_open)
      BIND_PANGU_FUNC_SAME_NAME(pangu_read1)
      BIND_PANGU_FUNC_SAME_NAME(pangu_uninit)
      BIND_PANGU_FUNC_SAME_NAME(pangu_write1)

      pread = [this](file_handle_t hfile, uint64_t offset, int n,
                     char* scratch) -> int64_t {
        int64_t fp = this->pangu_lseek(hfile, offset, SEEK_SET);
        if (fp < 0) {
          return fp;
        }
        return static_cast<int64_t>(this->pangu_read1(hfile, scratch, n, 0));
      };
      open_file = std::bind(pangu_open, _1, _2, _3, FILE_TYPE_NORMAL, _4);
      write = std::bind(pangu_write1, _1, _2, _3, 0);
      create = [this](const char* fname, int copys, int ftt,
                      int overwrite) -> int {
        return this->pangu_create1(fname, copys - ftt, copys, "BIGFILE_APPNAME",
                                   "BIGFILE_PARTNAME", overwrite, 0666,
                                   FILE_TYPE_NORMAL);
      };

#endif
#undef BIND_PANGU_FUNC
      static PanguGuard pangu_guard(this);

      // init pangu
      return Status::OK();
    };  // NOLINT

// libhdfs.so won't be in the standard locations. Use the path as specified
// in the libhdfs documentation.
#ifdef USE_PANGU2
    const char* kLibPanguDso = "libpangu2_api.so";
#else
    const char* kLibPanguDso = "libpangu_api.so";
#endif

    char* pangu_lib = getenv("PANGU_LIB");
    bool load = false;
    if (pangu_lib != nullptr) {
      VLOG(1) << "load " << kLibPanguDso << ", from PANGU_LIB " << pangu_lib
              << std::endl;
      string path = io::JoinPath(pangu_lib, kLibPanguDso);
      status_ = TryLoadAndBind(path.c_str(), &handle_);
      if (status_.ok()) load = true;
    }
    if (!load) {
      VLOG(1) << "load " << kLibPanguDso << ", from LD_LIBRARY_PATH"
              << std::endl;
      // try load libhdfs.so using dynamic loader's search path in case
      // libhdfs.so is installed in non-standard location
      status_ = TryLoadAndBind(kLibPanguDso, &handle_);
      if (status_.ok()) load = true;
    }

    if (!load) {
      status_ = errors::FailedPrecondition(
          "env PANGU_LIB not set, or libpangu_api.so not in LD_LIBRARY_PATH");
    }
  }

  Status status_;
  void* handle_ = nullptr;
};  // namespace tensorflow

PanguFileSystem::PanguFileSystem() : pangu_(LibPangu::Load()) {}

PanguFileSystem::~PanguFileSystem() {}

string PanguFileSystem::TranslateName(const string& name) const {
  StringPiece scheme, namenode, path;
  io::ParseURI(name, &scheme, &namenode, &path);
  return std::string(path);
}

Status GetCompleteURI(const string& name, string* uri) {
  StringPiece scheme, namenode, path;
  io::ParseURI(name, &scheme, &namenode, &path);
  if (scheme != "pangu") {
    return errors::FailedPrecondition(name + "'s scheme(" +
                                      std::string(scheme) + ") is not pangu");
  }
  if (namenode == "") {
    string cluster_name = GetEnvOrDefault<string>("PANGU_CLUSTER_NAME", "");
    if (cluster_name == "") {
      return errors::FailedPrecondition(
          name + "'s cluster_name is empty", ", please set PANGU_CLUSTER_NAME",
          " or use complete URI(pangu://<clusterName>/<path>)");
    }
    namenode = cluster_name;
  }
  *uri = "pangu://" + std::string(namenode) + io::CleanPath(path);
  return Status::OK();
}

class PanguRandomAccessFile : public RandomAccessFile {
 public:
  PanguRandomAccessFile(const std::string& fname, file_handle_t hfile,
                        LibPangu* pangu)
      : hfile_(hfile), pangu_(pangu) {
    GetCompleteURI(fname, &filename_);
  }

  ~PanguRandomAccessFile() override {
    VLOG(1) << "~PanguRandomAccessFile(), closed file Enter, " << filename_;
    if (hfile_ != nullptr) {
      pangu_->close_file(hfile_);
    }
    VLOG(1) << "~PanguRandomAccessFile(), closed file Leave, " << filename_;
    hfile_ = nullptr;
  }

  bool IsValid() { return hfile_ != nullptr; }

  // random access, read data from specified offset in file
  Status Read(uint64 offset, size_t n, StringPiece* result,
              char* scratch) const override {
    VLOG(1) << "PanguRandomAccessFile->Read() Enter, " << filename_;
    Status s;
    static int buffer_size = 1024 * 1024;  // 1 MB
    char* dst = scratch;
    bool eof_retried = false;
    while (n > 0 && s.ok()) {
      int to_read = n > static_cast<size_t>(buffer_size) ? buffer_size
                                                         : static_cast<int>(n);
      mutex_lock lock(mu_);
      int64_t bytes_read = pangu_->pread(hfile_, offset, to_read, dst);
      if (bytes_read > 0) {
        dst += bytes_read;
        n -= bytes_read;
        offset += bytes_read;
      } else if (bytes_read == 0 && !eof_retried) {
        // Always reopen the file upon reaching EOF to see if there's more data.
        // If writers are streaming contents while others are concurrently
        // reading, HDFS requires that we reopen the file to see updated
        // contents.
        //
        // Fixes #5438
        RET_IOERROR_IF_RC_NOT_ZERO(pangu_->close_file(hfile_), filename_);
        hfile_ = nullptr;
#ifdef USE_PANGU2
        RET_IOERROR_IF_RC_NOT_ZERO(
            pangu_->open_file(filename_.c_str(), O_RDONLY, 0, &hfile_),
            filename_);
#else
        RET_IOERROR_IF_RC_NOT_ZERO(
            pangu_->open_file(filename_.c_str(), FLAG_GENERIC_READ, 0, &hfile_),
            filename_);
#endif
        eof_retried = true;
      } else if (bytes_read == 0 && eof_retried) {
        s = Status(error::OUT_OF_RANGE, "Read less bytes than requested");
      } else if (bytes_read < 0) {
        s = IOError(filename_ + "Read failed", -bytes_read);
      }
    }
    VLOG(1) << "PanguRandomAccessFile->Read() Leave, " << filename_;
    *result = StringPiece(scratch, dst - scratch);
    return s;
  }

 private:
  string filename_;

  mutable mutex mu_;
  mutable file_handle_t hfile_;
  LibPangu* pangu_;
};

Status PanguFileSystem::Connect(const string& fname) {
  TF_RETURN_IF_ERROR(pangu_->status());
  return Status::OK();
}

Status PanguFileSystem::NewRandomAccessFile(
    const string& ori_fname, std::unique_ptr<RandomAccessFile>* result) {
  string fname;
  TF_RETURN_IF_ERROR(GetCompleteURI(ori_fname, &fname));
  TF_RETURN_IF_ERROR(Connect(fname));
  VLOG(1) << "PanguFileSystem->NewRandomAccessFile() Enter, " << fname;
  file_handle_t hfile;
#ifdef USE_PANGU2
  RET_IOERROR_IF_RC_NOT_ZERO(
      pangu_->open_file(fname.c_str(), O_RDONLY, 0, &hfile), fname);
#else
  RET_IOERROR_IF_RC_NOT_ZERO(
      pangu_->open_file(fname.c_str(), FLAG_GENERIC_READ, 0, &hfile), fname);
#endif
  result->reset(new PanguRandomAccessFile(fname, hfile, pangu_));
  VLOG(1) << "PanguFileSystem->NewRandomAccessFile() Leave, " << fname;
  return Status::OK();
}

class PanguWritableFile : public WritableFile {
 public:
  PanguWritableFile(const string& fname, file_handle_t hfile, bool syncwrite,
                    LibPangu* pangu)
      : hfile_(hfile), syncwrite_(syncwrite), pangu_(pangu) {
    GetCompleteURI(fname, &filename_);
  }

  ~PanguWritableFile() override {
    if (hfile_ != nullptr) {
      pangu_->close_file(hfile_);
      hfile_ = nullptr;
    }
  }

  Status Append(StringPiece data) override {
    VLOG(1) << "PanguWritableFile->Append() Enter, " << filename_;
    const char* src = data.data();
    size_t size = data.size();
    static int buffer_size = 1024 * 1024;  // 1MB
    while (size > 0) {
      int to_write = size > static_cast<size_t>(buffer_size)
                         ? buffer_size
                         : static_cast<int>(size);
      int bytes_wrote = pangu_->write(hfile_, src, to_write);
      if (bytes_wrote != to_write) {
        return IOError(filename_, -bytes_wrote);
      }
      size -= bytes_wrote;
      src += bytes_wrote;
    }
    VLOG(1) << "PanguWritableFile->Append() Leave, " << filename_;
    return Status::OK();
  }

  Status Flush() override {
    VLOG(1) << "PanguWritableFile->Flush() " << filename_;
    return Sync();
  }

  Status Sync() override {
    VLOG(1) << "PanguWritableFile->Sync() Enter, " << filename_;
    if (!syncwrite_) {
      int rc = pangu_->fsync(hfile_);
      if (rc < 0) {
        return IOError("PanguWritableFile->Sync() failed, " + filename_, -rc);
      }
    }
    VLOG(1) << "PanguWritableFile->Sync() Leave," << filename_;
    return Status::OK();
  }

  Status Close() override {
    VLOG(1) << "PanguWritableFile->Close() Enter, " << filename_;
    RET_IOERROR_IF_RC_NOT_ZERO(pangu_->close_file(hfile_), filename_);
    VLOG(1) << "PanguWritableFile->Close() " << filename_;
    hfile_ = nullptr;
    return Status::OK();
  }

 private:
  string filename_;
  file_handle_t hfile_;
  bool syncwrite_;
  LibPangu* pangu_;
};

Status PanguFileSystem::NewWritableFileInternal(
    const string& ori_fname, std::unique_ptr<WritableFile>* result,
    int overwrite) {
  string fname;
  TF_RETURN_IF_ERROR(GetCompleteURI(ori_fname, &fname));
  TF_RETURN_IF_ERROR(Connect(fname));
  VLOG(1) << "PanguWritableFile->NewWritableFileInternal() Enter, " << fname;

  bool syncwrite = GetEnvOrDefault<bool>("PANGU_USE_DIRECT_WRITES", false);
  int copys = GetEnvOrDefault<int>("PANGU_COPYS", 3);
  int ftt = GetEnvOrDefault<int>("PANGU_FTT", 1);

  file_handle_t hfile;
  int rc = pangu_->create(fname.c_str(), copys, ftt, overwrite);
  if (rc != 0 && rc != -EEXIST) {
    return IOError("PanguWritableFile->create() failed, " + fname, -rc);
  }

#ifdef USE_PANGU2
  int flag = syncwrite ? O_WRONLY | O_SYNC : O_WRONLY;
  int mode = GetEnvOrDefault<int>("PANGU_WRITE_MODE", 1);
  int o_mode = mode == 2 ? OPEN_MODE_Y_WRITE : OPEN_MODE_STAR_WRITE;
#else
  int flag = FLAG_SEQUENTIAL_WRITE;
  int o_mode = 0;
#endif
  RET_IOERROR_IF_RC_NOT_ZERO(
      pangu_->open_file(fname.c_str(), flag, o_mode, &hfile), fname);

  result->reset(new PanguWritableFile(fname, hfile, syncwrite, pangu_));
  VLOG(1) << "PanguWritableFile->NewWritableFileInternal() Leave," << fname;
  return Status::OK();
}

Status PanguFileSystem::NewWritableFile(const string& fname,
                                        std::unique_ptr<WritableFile>* result) {
  VLOG(1) << "PanguWritableFile->NewWritableFile() " << fname;
  // set O_WRONLY
  return NewWritableFileInternal(fname, result, 1);
}

Status PanguFileSystem::NewAppendableFile(
    const string& fname, std::unique_ptr<WritableFile>* result) {
  VLOG(1) << "PanguWritableFile->NewAppendableFile() " << fname;
  // set O_WRONLY|O_APPEND
  return NewWritableFileInternal(fname, result, 0);
}

Status PanguFileSystem::NewReadOnlyMemoryRegionFromFile(
    const string& fname, std::unique_ptr<ReadOnlyMemoryRegion>* result) {
  TF_RETURN_IF_ERROR(Connect(fname));
  // hadoopReadZero() technically supports this call with the following
  // caveats:
  // - It only works up to 2 GB. We'd have to Stat() the file to ensure that
  //   it fits.
  // - If not on the local filesystem, the entire file will be read, making
  //   it inefficient for callers that assume typical mmap() behavior.
  return errors::Unimplemented("PANGU does not support ReadOnlyMemoryRegion");
}

Status PanguFileSystem::FileExists(const string& fname) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(fname, &complete_path));
  VLOG(1) << "PanguFileSystem->FileExists() Enter," << complete_path;

  TF_RETURN_IF_ERROR(Connect(complete_path));
  file_status_t stat;
  int rc = pangu_->get_status(complete_path.c_str(), &stat);
  VLOG(1) << "PanguFileSystem->FileExists() Leave," << complete_path;
  if (rc == -ENOENT) {
    return errors::NotFound(fname, " not found.");
  } else if (rc == 0) {
    return Status::OK();
  } else {
    return IOError(
        "PanguFileSystem->FileExists() get_status() failed, " + fname, -rc);
  }
}

void FixDirectoryName(std::string* name) {
  CHECK(name != nullptr);
  int len = name->length();
  if (len && name->at(len - 1) != '/') {
    name->push_back('/');
  }
}

Status PanguFileSystem::GetChildren(const std::string& path,
                                    std::vector<std::string>* result) {
  VLOG(1) << "PanguFileSystem->GetChildren() Enter, " << path;
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(path, &complete_path));
  FixDirectoryName(&complete_path);
  pangu_dir_t hdir;

  TF_RETURN_IF_ERROR(Connect(complete_path));
  int rc = pangu_->open_dir(complete_path.c_str(), &hdir, 4096);
  if (rc == -ENOENT) {
    return errors::NotFound(path, " not found, complete_path:", complete_path);
  } else if (rc != 0) {
    return IOError("PanguFileSystem->GetChildren(): open_dir() failed, " + path,
                   -rc);
  }

  result->clear();
  file_status_t stat;
  char name[1024];
  while (rc == 0) {
    int length = sizeof(name) - 1;
    rc = pangu_->read_dir(hdir, name, &length, &stat);
    if (rc == 0) {
      if (length > 0 && name[length - 1] == '/') {
        length--;
      }
      name[length] = '\0';
      result->push_back(std::string(io::Basename(name)));
    } else if (rc < 0) {
      break;
    }
  }
  RET_IOERROR_IF_RC_NOT_ZERO(pangu_->close_dir(hdir), path);
  VLOG(1) << "PanguFileSystem->GetChildren() Leave," << path;
  return Status::OK();
}

Status PanguFileSystem::GetMatchingPaths(const string& pattern,
                                         std::vector<string>* results) {
  return internal::GetMatchingPaths(this, Env::Default(), pattern, results);
}

Status PanguFileSystem::DeleteFile(const string& fname) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(fname, &complete_path));

  TF_RETURN_IF_ERROR(Connect(complete_path));
  VLOG(1) << "PanguFileSystem->DeleteFile() Enter," << complete_path;
  RET_IOERROR_IF_RC_NOT_ZERO(
      pangu_->remove_file_or_dir(complete_path.c_str(), 0), fname);
  VLOG(1) << "PanguFileSystem->DeleteFile() Leave," << complete_path;
  return Status::OK();
}

Status PanguFileSystem::CreateDir(const string& name) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(name, &complete_path));
  FixDirectoryName(&complete_path);

  TF_RETURN_IF_ERROR(Connect(complete_path));
  VLOG(1) << "PanguFileSystem->CreateDir() Enter," << complete_path;
  RET_IOERROR_IF_RC_NOT_ZERO(pangu_->mkdir(complete_path.c_str(), 0777), name);
  VLOG(1) << "PanguFileSystem->CreateDir() Leave," << complete_path;
  return Status::OK();
}

Status PanguFileSystem::DeleteDir(const std::string& name) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(name, &complete_path));
  FixDirectoryName(&complete_path);

  VLOG(1) << "PanguFileSystem->DeleteDir() Enter," << complete_path;

  TF_RETURN_IF_ERROR(Connect(complete_path));
  std::vector<string> children;
  TF_RETURN_IF_ERROR(GetChildren(name, &children));
  if (children.size() > 0) {
    return errors::FailedPrecondition("Cannot delete a non-empty directory.");
  }

  int rc = pangu_->rmdir(complete_path.c_str(), 0);
  if (rc != 0 && rc != -ENOENT) {
    return IOError("PanguFileSystem->DeleteDir(): rmdir() failed, " + name,
                   -rc);
  }
  VLOG(1) << "PanguFileSystem->DeleteDir() Leave," << complete_path;
  return Status::OK();
}

Status PanguFileSystem::DeleteRecursively(const string& dirname,
                                          int64* undeleted_files,
                                          int64* undeleted_dirs) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(dirname, &complete_path));
  FixDirectoryName(&complete_path);

  TF_RETURN_IF_ERROR(Connect(complete_path));
  int rc = pangu_->rmdir(complete_path.c_str(), 0);
  if (rc != 0 && rc != -ENOENT) {
    return IOError(
        "PanguFileSystem->DeleteRecursively(): rmdir() failed, " + dirname,
        -rc);
  }
  *undeleted_files = 0;
  *undeleted_dirs = 0;
  return Status::OK();
}

Status PanguFileSystem::GetFileSize(const std::string& fname, uint64* size) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(fname, &complete_path));

  TF_RETURN_IF_ERROR(Connect(complete_path));
  VLOG(1) << "PanguFileSystem->GetFileSize() Enter," << complete_path;
  *size = 0L;
  file_status_t stat;
  RET_IOERROR_IF_RC_NOT_ZERO(pangu_->get_status(complete_path.c_str(), &stat),
                             fname);
  *size = stat.file_length;
  VLOG(1) << "PanguFileSystem->GetFileSize() Leave," << complete_path;
  return Status::OK();
}

// The rename is not atomic. Pangu does not allow a renaming if the
// target already exists. So, we delete the target before attempting the
// rename.
Status PanguFileSystem::RenameFile(const std::string& src,
                                   const std::string& target) {
  string src_path, des_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(src, &src_path));
  TF_RETURN_IF_ERROR(GetCompleteURI(target, &des_path));

  TF_RETURN_IF_ERROR(Connect(src_path));
  VLOG(1) << "PanguFileSystem->RenameFile() Enter, from " << src << " -> "
          << target << std::endl;
  if (FileExists(des_path) == Status::OK()) {
    RET_IOERROR_IF_RC_NOT_ZERO(pangu_->remove_file_or_dir(des_path.c_str(), 0),
                               des_path);
  }
  if (IsDirectory(src) == Status::OK()) {
    // when rename_dir, src_path and dst_path should end with '/', which is
    // constrained by PanguFileSystem
    RET_IOERROR_IF_RC_NOT_ZERO(pangu_->rename_dir(src_path.append("/").c_str(),
                                                  des_path.append("/").c_str()),
                               src + "->" + target);
  } else {
    RET_IOERROR_IF_RC_NOT_ZERO(
        pangu_->rename_file(src_path.c_str(), des_path.c_str()),
        src + "->" + target);
  }
  VLOG(1) << "PanguFileSystem->RenameFile() Leave, from " << src << " -> "
          << target << std::endl;
  return Status::OK();
}

Status PanguFileSystem::Stat(const std::string& fname, FileStatistics* stats) {
  string complete_path;
  TF_RETURN_IF_ERROR(GetCompleteURI(fname, &complete_path));

  TF_RETURN_IF_ERROR(Connect(complete_path));
  VLOG(1) << "PanguFileSystem->Stat() Enter, " << fname;
  file_status_t stat;
  RET_IOERROR_IF_RC_NOT_ZERO(pangu_->get_status(complete_path.c_str(), &stat),
                             complete_path);
  stats->mtime_nsec = static_cast<int64>(stat.modified_time) * 1e9;
  stats->length = static_cast<int64>(stat.file_length);
  stats->is_directory = stat.is_dir == 1;

  VLOG(1) << "PanguFileSystem->Stat() Leave, " << fname;
  return Status::OK();
}
}  // namespace tensorflow
