// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#ifndef THIRD_PARTY_PANGU_PANGU_API_H_
#define THIRD_PARTY_PANGU_PANGU_API_H_
/* Explicitly enable the UINT64_MAX macros */
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>
#include <stdio.h>  // for SEEK_SET, SEEK_CUR, SEEK_END

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _file_status {
  uint64_t file_length;    // file length
  int is_dir;              // 1 indicate it is a dir, 0 is file
  int copys;               // number of min replica for the file
  uint64_t create_time;    // create time of the file. From 1970.1.1
  uint64_t modified_time;  // modify time for the file
  // newly added to support NFS
  uint64_t file_id;    // file id, could be deemed as inode id
  uint32_t hardlinks;  // number of hard links of the file
  int file_flag;       // file flag, internal usage only now
  uint8_t file_attr;   // file attr, internal usage only
  uint16_t access;     // access permission of the file
  uint32_t owner;      // owner id of the file
  uint32_t group;      // group id of the file
} file_status_t;

typedef struct _dir_status {
  uint64_t dir_count;
  uint64_t file_count;
  uint64_t space_size;
  int64_t space_quota;
  int64_t files_quota;
} dir_status_t;

typedef void* pangu_dir_t;
typedef struct _file_handle_obj {
  void* stream_obj;  // stream object for the file
  int file_type;     // file type defined below
  int rw_flags;      // read or write flag defined below
  int need_seek;     // whether need to do seek before read/write
  uint64_t offset;   // offset for the next read or write operation
} file_handle_obj, *file_handle_t;

typedef void* pangu_chunk_handle_t;
typedef struct _chunk_location {
#define MAX_CHUNK_LOC_SERVERS 64
  uint64_t chunk_attr;  // chunk attribute such as EC, etc
  char*
      chunk_server[MAX_CHUNK_LOC_SERVERS];  // list of hostname for chunk server
  uint64_t block_offset;  // offset for the chunk start in the file
  uint64_t block_length;  // in pangu, it is chunk size
} chunk_location_t;

// open flag
enum {
  FLAG_GENERIC_READ = 0x1,
  FLAG_GENERIC_WRITE = 0x2,  // not implemented
  FLAG_SEQUENTIAL_READ = 0x4,
  FLAG_SEQUENTIAL_WRITE = 0x8,
  FLAG_READ_ONE_LOG = 0x10,
  FLAG_READ_BATCH_LOG = 0x20,
  FLAG_READ_LOG_WITH_CHECKSUM = 0x40,
  FLAG_READ_WITH_BACKUP1 = 0x80,
  FLAG_WRITE_USE_CACHE = 0x100
};

enum {
  FILE_TYPE_NORMAL = 0,
  FILE_TYPE_RECORD = 1,  // not implemented
  FILE_TYPE_LOGFILE = 2,
  FILE_TYPE_RAIDFILE = 3  // not implemented
};

int pangu_init(const char* uri, int perm);

int pangu_uninit();

int pangu_set_user_group(uint32_t owner, uint32_t group);

int pangu_create(const char* path, int min_copys, int max_copys,
                 const char* app_name, const char* part_name, int overwrite,
                 int mode);

int pangu_create1(const char* path, int min_copys, int max_copys,
                  const char* app_name, const char* part_name, int overwrite,
                  int mode, int file_type);

int pangu_open(const char* path, int flag, int mode, int file_type,
               file_handle_t* fhandle);

int pangu_close(file_handle_t fhandle);

int pangu_read(file_handle_t fhandle, char* buf, int size);

int pangu_read1(file_handle_t fhandle, char* buf, int size, int opt);

int pangu_write(file_handle_t fhandle, const char* buf, int size);

int pangu_write1(file_handle_t fhandle, const char* buf, int size, int opt);

int pangu_fsync(file_handle_t fhandle);

int64_t pangu_lseek(file_handle_t fhandle, int64_t offset, int whence);

int pangu_remove(const char* path, int permanent);

int pangu_mkdir(const char* path, int mode);

int pangu_rmdir(const char* path, int permanent);

int pangu_dir_exist(const char* dir_path);

int pangu_file_exist(const char* file_path);

int pangu_get_status(const char* path, file_status_t* status);

int pangu_dir_status(const char* path, dir_status_t* status);

int pangu_open_dir(const char* dir_path, pangu_dir_t* dir_handle,
                   int list_batch);

int pangu_read_dir(pangu_dir_t dir_handle, char* name, int* name_len,
                   file_status_t* status);

int pangu_close_dir(pangu_dir_t dir_handle);

int pangu_file_time(const char* file_path, uint64_t* create_time,
                    uint64_t* mtime);

int pangu_file_length(const char* file_path, uint64_t* len);

int pangu_dir_length(const char* dir_path, uint64_t* len);

int pangu_append(const char* path, const char* buf, int size, int sync);

int pangu_pread(const char* path, char* buf, int size, uint64_t offset);

int pangu_truncate(const char* path, uint64_t new_size);

int pangu_rename_file(const char* src_name, const char* dst_name);

int pangu_rename_dir(const char* src_name, const char* dst_name);

int pangu_chmod(const char* path, int mode);

int pangu_chown(const char* path, uint32_t owner, uint32_t group);

int pangu_utime(const char* path, uint64_t mtime);

int pangu_setxattr(const char* path, const char* name, const void* value,
                   int size, int flags);

int pangu_getxattr(const char* path, const char* name, void* value, int size);

int pangu_listxattr(const char* path, char* list, int size);

int pangu_open_block_location(const char* path, uint64_t offset,
                              uint64_t length, pangu_chunk_handle_t* handle);

int pangu_next_block_location(pangu_chunk_handle_t handle,
                              chunk_location_t* chunk_loc);

int pangu_close_block_location(pangu_chunk_handle_t handle);

int pangu_set_flag(const char* flag_name, const void* value, int size);

int pangu_get_flag(const char* flag_name, void* value, int size);

#ifdef __cplusplus
}
#endif

#endif  // THIRD_PARTY_PANGU_PANGU_API_H_
