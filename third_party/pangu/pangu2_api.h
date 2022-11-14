#ifndef _H_PANGU2_API_
#define _H_PANGU2_API_
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _dir_status {
    uint64_t dir_count;
    uint64_t file_count;
} dir_status_t;

typedef struct _file_status {
    uint64_t file_length;
    int is_dir;
    int copys;
    uint64_t create_time;
    uint64_t modified_time;
    // newly added to support NFS
    uint32_t hardlinks;
    int file_flag;
    uint8_t file_attr;
    dir_status_t dir_status;
} file_status_t;

typedef struct _fs_status {
    uint64_t total_size;
    uint64_t free_size;
} fs_status_t;

typedef struct _chunk_location {
#define MAX_CHUNK_LOC_SERVERS	64
    uint64_t chunk_attr;      		// chunk attribute such as EC, etc
    char* chunk_server[MAX_CHUNK_LOC_SERVERS];   // list of hostname for chunk server
    uint64_t block_offset;    		// offset for the chunk start in the file
    uint64_t block_length;    		// in pangu, it is chunk size
} chunk_location_t;

typedef struct _file_handle_t
{
    uint64_t low;
    uint64_t high;
} *file_handle_t;

typedef void* pangu_dir_t;
typedef void* chunk_handle_t;

// write mode
enum {
    OPEN_MODE_STAR_WRITE        = 0x1,
    OPEN_MODE_Y_WRITE           = 0x2
};

enum {
    FLAG_EC_FILE = 0x1,
    FLAG_EC_FILE_WITH_PKG_4K    = 0x2 | FLAG_EC_FILE,
    FLAG_EC_FILE_WITH_PKG_16K   = 0x4 | FLAG_EC_FILE,
    FLAG_EC_FILE_WITH_PKG_32K   = 0x8 | FLAG_EC_FILE,
    FLAG_EC_FILE_WITH_PKG_64K  = 0x10 | FLAG_EC_FILE,
    FLAG_EC_FILE_WITH_PKG_128K = 0x20 | FLAG_EC_FILE,
    FLAG_EC_FILE_WITH_PKG_1M   = 0x40 | FLAG_EC_FILE
};

int pangu2_init(const char* uri, int flag);

int pangu2_uninit();

int pangu2_create(const char* path, int copys, int ftt, const char* placement, int overwrite);

int pangu2_create1(const char* path, int copys, int ftt, const char* placement, int overwrite, int recursive, int flags);

int pangu2_open(const char* path, int flag, int o_mode, file_handle_t* fhandle);

int pangu2_close(file_handle_t fhandle);

int pangu2_append(file_handle_t fhandle, const char* buf, int size);

int pangu2_appendv(file_handle_t fhandle, const struct iovec *vector, int count);

int pangu2_pread(file_handle_t fhandle, char* buf, int size, uint64_t offset);

int pangu2_preadv(file_handle_t fhandle, struct iovec *vector, int count, uint64_t offset);

int pangu2_fsync(file_handle_t fhandle);

int pangu2_remove(const char* path, int permanent);

int pangu2_mkdir(const char* path, int mode);

int pangu2_rmdir(const char* path);

int pangu2_rmdir1(const char* path, int permanent);

int pangu2_get_status(const char* path, file_status_t* status);

int pangu2_stat_fs(const char* path, fs_status_t* status);

int pangu2_open_dir(const char* dir_path, pangu_dir_t* dir_handle, int list_batch);

int pangu2_read_dir(pangu_dir_t dir_handle, char* name, int* name_len, file_status_t* status);

int pangu2_close_dir(pangu_dir_t dir_handle);

int pangu2_rename(const char* src_name, const char* dst_name);

int pangu2_link(const char* src_name, const char* dst_name);

int pangu2_seal_file(const char* path);

int pangu2_release_filelock(const char* path);

int pangu2_setxattr(const char* path, const char* name, const void* value,
                    int size, int flags);

int pangu2_getxattr(const char* path, const char* name, void* value, int size);

int pangu2_listxattr(const char* path, char* list, int size);

int pangu2_next_block_location(chunk_handle_t handle, chunk_location_t* chunk_loc);

int pangu2_close_block_location(chunk_handle_t handle);

int pangu2_set_flag(const char* flag_name, const void* value, int size);

int pangu2_get_flag(const char* flag_name, void* value, int size);

int pangu2_get_service_address(const char* path, char* buf, int size);

int pangu2_get_service_name(const char* path, char* buf, int size);

#ifdef __cplusplus
}
#endif

#endif //_H_PANGU2_API_
