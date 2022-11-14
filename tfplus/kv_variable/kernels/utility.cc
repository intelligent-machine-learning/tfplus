// Copyright 2020 The TF-plus Authors. All Rights Reserved.
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <utility>
#include <cerrno>
#include <cstring>
#include <regex>
#include "tensorflow/core/platform/default/logging.h"
#include "tfplus/kv_variable/kernels/utility.h"
#include "tensorflow/core/lib/io/path.h"
// #include "common/Utils.h" // from phstore?
#include "libarchive/archive.h"
#include "libarchive/archive_entry.h"

namespace tfplus {
using ::tensorflow::uint16;

// Get current unix time, by default divided by 3600*24 as days.
uint16 GetCurrentUnixTimeByDivisor(uint64_t divisor) {
  return static_cast<uint16>(std::time(nullptr) / divisor);
}

uint16_t GetUint16FromUint32(const uint32_t& source, bool isLow16Bits) {
  return isLow16Bits ? static_cast<uint16_t>(source & 0xFFFF)
                     : static_cast<uint16_t>(source >> 16);
}

uint32_t MakeUint32FromUint16(uint16_t high, uint16_t low) {
  return (((uint32_t)high << 16) | (uint32_t)low);
}
/*
std::string GetLegalPhstoreTableName(const std::string& s) {
  std::stringstream tmp;
  for (auto ch : s) {
    if (std::isalnum(ch)) {
      tmp << ch;
    }
  }
  return tmp.str();
}*/
Status GetChildren(const string& dir, std::vector<string>* result) {
  return ::tensorflow::Env::Default()->GetChildren(dir, result);
}
/*
Status GetChildrenWithParentDir(const string& dir,
                                std::vector<string>* result) {
  auto status = GetChildren(dir, result);
  if (!status.ok()) {
    LOG(FATAL) << "GetChildrenWithParentDir failed:" << dir;
    return status;
  }
  for (auto it = result->begin(); it != result->end(); ++it) {
    *it = JoinPath(dir, *it);
  }
  auto filelist_str = ConcatStringList(*result);
  LOG(INFO) << "GetChildrenWithParentDir successfully: " << filelist_str;
  return status;
}

Status CopyFiles(Env* env, const std::vector<std::string>& src_filenames,
                 const std::string& dest_dir,
                 std::vector<std::string>* dst_filenames) {
  auto status = env->FileExists(dest_dir);
  if (!status.ok()) {
    status = env->RecursivelyCreateDir(dest_dir);
    if (!status.ok()) {
      LOG(FATAL) << "Create directory " << dest_dir << " failed.";
      return status;
    }
    LOG(INFO) << "Create directory " << dest_dir << " successfully.";
  }
  for (auto& src_filename : src_filenames) {
    auto short_filename = GetShortFileName(src_filename);
    auto dst_filename = JoinPath(dest_dir, short_filename);
    status = env->CopyFile(src_filename, dst_filename);
    dst_filenames->emplace_back(std::move(dst_filename));
    if (!status.ok()) {
      return status;
    }
  }
  return Status::OK();
}

std::string GenerateTarFileName(const std::string& table_name) {
  return GetLegalPhstoreTableName(table_name) + ".tar";
}
*/
Status CreateTarFile(const char* tarname,
                     const std::vector<std::string>* filename) {
  struct archive* a;
  struct archive_entry* entry;
  struct stat st;
  char buff[1 << 20];
  int len;
  int fd;

  a = archive_write_new();
  archive_write_set_format_pax_restricted(a);
  if (archive_write_open_filename(a, tarname) != ARCHIVE_OK) {
    LOG(FATAL) << "open " << tarname << "filed!";
    return errors::Internal("open tar file failed: ", tarname);
  }
  for (auto& file : *filename) {
    if (stat(file.c_str(), &st) != 0) {
      return errors::Internal("file failed: ", file);
    }
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, file.c_str());
    archive_entry_set_size(entry, st.st_size);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    std::string absolute_path = archive_entry_pathname(entry);
    size_t first_pos = absolute_path.find_last_of("/") + 1;
    std::string relative_path =
        absolute_path.substr(first_pos, absolute_path.length() - first_pos);
    archive_entry_set_pathname(entry, relative_path.c_str());
    if (archive_write_header(a, entry) != ARCHIVE_OK) {
      LOG(FATAL) << "libarchive write header " << absolute_path << "error!";
      return errors::Internal("libarchive write header error: ", absolute_path);
    }
    fd = open(file.c_str(), 0);
    if (fd < 0) {
      return errors::Internal("open file error", file);
    }
    do {
      len = read(fd, buff, sizeof(buff));
      if (len < 0) {
        return errors::Internal("read file error", file);
      }
      if (len == 0) break;
      if (archive_write_data(a, buff, len) != len) {
        return errors::Internal("error writing output archive ", file);
      }
    } while (len > 0);
    if (close(fd) < 0) {
      return errors::Internal("close file error", file);
    }
    archive_entry_free(entry);
  }
  if (archive_write_close(a) != ARCHIVE_OK) {
    return errors::Internal("archive write close error", tarname);
  }
  if (archive_write_free(a) != ARCHIVE_OK) {
    return errors::Internal("archive write free error", tarname);
  }
  return Status::OK();
}

int copy_data(struct archive* ar, struct archive* aw) {
  int r;
  const void* buff;
  size_t size;
  la_int64_t offset;

  for (;;) {
    r = archive_read_data_block(ar, &buff, &size, &offset);
    if (r == ARCHIVE_EOF) return (ARCHIVE_OK);
    if (r < ARCHIVE_OK) return (r);
    r = archive_write_data_block(aw, buff, size, offset);
    if (r < ARCHIVE_OK) {
      return (r);
    }
  }
}
/*
Status ExtractTarFileToDirectory(const std::string& tar_file,
                                 const std::string& directory_path) {
  struct archive* a;
  struct archive* ext;
  struct archive_entry* entry;
  int flags;
  int r;
  //Select which attributes we want to restore. 
  flags = ARCHIVE_EXTRACT_TIME;
  flags |= ARCHIVE_EXTRACT_PERM;
  flags |= ARCHIVE_EXTRACT_ACL;
  flags |= ARCHIVE_EXTRACT_FFLAGS;

  a = archive_read_new();
  archive_read_support_format_all(a);
  ext = archive_write_disk_new();
  archive_write_disk_set_options(ext, flags);
  archive_write_disk_set_standard_lookup(ext);
  r = archive_read_open_filename(a, tar_file.c_str(), 10240);
  if (r != ARCHIVE_OK) {
    return errors::Internal("open tar_file failed", tar_file);
  }
  while (true) {
    r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) break;
    if (r < ARCHIVE_OK) {
      return errors::Internal("archive_write_finish_entry error");
    }
    std::string currentFile = archive_entry_pathname(entry);
    const std::string fullOutputPath = JoinPath(directory_path, currentFile);
    archive_entry_set_pathname(entry, fullOutputPath.c_str());
    r = archive_write_header(ext, entry);
    if (r < ARCHIVE_OK) {
      LOG(FATAL) << "open write header " << currentFile << "filed!";
      return errors::Internal("open write header failed: ", currentFile);
    }
    if (archive_entry_size(entry) > 0) {
      r = copy_data(a, ext);
      if (r < ARCHIVE_OK) {
        LOG(FATAL) << "copy_data failed from " << tar_file << " to "
                   << currentFile;
        return errors::Internal("copy_data failed from ", tar_file, " to ",
                                currentFile);
      }
    }
    r = archive_write_finish_entry(ext);
    if (r < ARCHIVE_OK) {
      return errors::Internal("archive_write_finish_entry error");
    }
  }
  if (archive_read_close(a) != ARCHIVE_OK) {
    return errors::Internal("archive read close error", tar_file);
  }
  if (archive_read_free(a) != ARCHIVE_OK) {
    return errors::Internal("archive read free error", tar_file);
  }
  if (archive_write_close(ext) != ARCHIVE_OK) {
    return errors::Internal("archive write close error", directory_path);
  }
  if (archive_write_free(ext) != ARCHIVE_OK) {
    return errors::Internal("archive write free error", directory_path);
  }
  return Status::OK();
}
*/
std::string ConcatStringList(const std::vector<std::string>& string_list) {
  std::string result = "[";
  for (auto&& s : string_list) {
    result += s + ", ";
  }
  result += "]";
  return result;
}
/*
void DeleteDirectory(const std::string& directory) {
  PH::Utils::DeleteDir(directory.c_str());
}

std::string GetShortFileName(const std::string& filename) {
  return PH::Utils::GetShortFileName(filename);
}

std::string JoinPath(const std::string& dir, const std::string& name) {
  return PH::Utils::JoinPath(dir, name);
}
*/
std::string GenerateSnapshotPath(const std::string& prefix) {
  return prefix + ".snapshot";
}

std::string RemoveCheckpointPathTempSuffix(const std::string& path) {
  // example: _temp_d1b6a51df8a84b92a12ffa7bf271437a/part-00000-of-00020
  std::regex suffix_regex("_temp_[\\da-f]{32}/part-[\\d]{5}-of-[\\d]{5}$");
  std::cmatch m;
  if (!std::regex_search(path.c_str(), m, suffix_regex)) {
    return path;
  } else {
    return m.prefix();;
  }
}

bool CanMakeSymlink(const std::vector<std::string>& pathnames,
                    const std::string& dest_dir) {
  if (dest_dir.rfind("/", 0) != 0) {
    LOG(WARNING) << "can't make symbolic link because directory " << dest_dir
                 << " is not a local directory";
    return false;
  }
  std::regex schema("(dfs)|(pangu)|(oss)");
  for (auto& pathname : pathnames) {
    if (pathname.rfind("/", 0) != 0) {
      LOG(WARNING) << "can't make symbolic link because " << pathname
                   << " is not a local file";
      return false;
    }
    std::cmatch m;
    if (std::regex_search(pathname.c_str(), m, schema)) {
      LOG(WARNING) << "can't make symbolic link because " << pathname
                   << " is not a local file";
      return false;
    }
    struct stat sb;
    if (!(stat(pathname.c_str(), &sb) == 0 &&
          (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode)))) {
      LOG(WARNING) << "can't make symbolic link because " << pathname
                   << " is not a local file";
      return false;
    }
  }
  return true;
}
/*
inline Status SymlinkInternal(const std::vector<std::string>& files,
                              const std::string& targetDir) {
  for (const auto& file : files) {
    std::string newFile = JoinPath(targetDir, GetShortFileName(file));
    if (symlink(file.c_str(), newFile.c_str()) != 0) {
      LOG(ERROR) << "Fail to make symbolic from file " << file << " to "
                 << targetDir << std::strerror(errno);
      return errors::Internal(std::strerror(errno));
    }
  }
  return Status::OK();
}

Status SymlinkFiles(Env* env, const std::vector<std::string>& files,
                    const std::string& dest_dir,
                    std::vector<std::string>* dst_filenames) {
  if (dest_dir.rfind("/", 0) != 0) {
    return errors::Internal(
        "can't make symbolic link because directory is not a local "
        "directory: ",
        dest_dir);
  }
  auto status = env->FileExists(dest_dir);
  if (!status.ok()) {
    status = env->RecursivelyCreateDir(dest_dir);
    if (!status.ok()) {
      LOG(FATAL) << "Create directory " << dest_dir << " failed.";
      return status;
    }
    LOG(INFO) << "Create directory " << dest_dir << " successfully.";
  }
  struct stat s;
  if (!(stat(dest_dir.c_str(), &s) == 0) && (s.st_mode & S_IFDIR)) {
    return errors::Internal(
        "can't make symbolic link because directory is not a local "
        "directory: ",
        dest_dir);
  }
  for (auto& file : files) {
    auto short_fname = GetShortFileName(file);
    auto dst_fname = JoinPath(dest_dir, short_fname);
    dst_filenames->emplace_back(dst_fname);
    LOG(INFO) << "Make symbolic link from " << file << " to " << dst_fname;
  }
  TF_RETURN_IF_ERROR(SymlinkInternal(files, dest_dir));
  return Status::OK();
}
*/
Status Unlink(const std::string& path) {
  if (unlink(path.c_str()) != 0) {
    return ::tensorflow::errors::Internal("Failed to unlink file: ", path);
  }
  return Status::OK();
}
}  // namespace tfplus
