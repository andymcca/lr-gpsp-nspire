/* newlib has no folders: just enough for libretro-common's
 * vfs_implementation.c, which the core run under qemu-arm links in. */
#ifndef SHIM_DIRENT_H
#define SHIM_DIRENT_H
struct dirent { char d_name[256]; unsigned char d_type; };
typedef struct shim_dir DIR;
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10
static inline DIR *opendir(const char *n) { (void)n; return 0; }
static inline struct dirent *readdir(DIR *d) { (void)d; return 0; }
static inline int closedir(DIR *d) { (void)d; return 0; }
#endif
