/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "userland/haiku_compat.h"
#include <OS.h>
#include <fs_info.h>
#include <driver_settings.h>
#include <IconUtils.h>

#include <sys/syscall.h>
#include <sys/stat.h>
#include <image.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <mntent.h>

namespace {

struct InodePathEntry {
    ino_t ino;
    char path[B_PATH_NAME_LENGTH];
};

static constexpr size_t kMaxInodePaths = 256;
static InodePathEntry sInodePaths[kMaxInodePaths];
static size_t sInodePathCount = 0;
static size_t sInodePathNext = 0;
static pthread_mutex_t sInodePathMutex = PTHREAD_MUTEX_INITIALIZER;

static bool get_fd_path(int fd, char* buf, size_t bufSize)
{
    if (fd < 0 || !buf || bufSize == 0)
        return false;
    char procLink[64];
    snprintf(procLink, sizeof(procLink), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(procLink, buf, bufSize - 1);
    if (len <= 0)
        return false;
    buf[len] = '\0';
    return true;
}

static void record_fd_dir_inode(int fd)
{
    if (fd < 0)
        return;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode))
        return;
    char dirPath[B_PATH_NAME_LENGTH];
    if (!get_fd_path(fd, dirPath, sizeof(dirPath)))
        return;

    pthread_mutex_lock(&sInodePathMutex);
    for (size_t i = 0; i < sInodePathCount; i++) {
        if (sInodePaths[i].ino == st.st_ino) {
            strncpy(sInodePaths[i].path, dirPath, B_PATH_NAME_LENGTH - 1);
            sInodePaths[i].path[B_PATH_NAME_LENGTH - 1] = '\0';
            pthread_mutex_unlock(&sInodePathMutex);
            return;
        }
    }
    size_t idx = sInodePathNext;
    sInodePaths[idx].ino = st.st_ino;
    strncpy(sInodePaths[idx].path, dirPath, B_PATH_NAME_LENGTH - 1);
    sInodePaths[idx].path[B_PATH_NAME_LENGTH - 1] = '\0';
    sInodePathNext = (sInodePathNext + 1) % kMaxInodePaths;
    if (sInodePathCount < kMaxInodePaths)
        sInodePathCount++;
    pthread_mutex_unlock(&sInodePathMutex);
}

static bool lookup_dir_inode(ino_t ino, char* outPath, size_t outSize)
{
    pthread_mutex_lock(&sInodePathMutex);
    for (size_t i = 0; i < sInodePathCount; i++) {
        if (sInodePaths[i].ino == ino) {
            strncpy(outPath, sInodePaths[i].path, outSize - 1);
            outPath[outSize - 1] = '\0';
            pthread_mutex_unlock(&sInodePathMutex);
            return true;
        }
    }
    pthread_mutex_unlock(&sInodePathMutex);
    return false;
}

}  // namespace

struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};

extern "C" {

int32 _kern_read_dir(int fd, struct dirent *buffer, size_t bufferSize, int32 maxCount)
{
    if (!buffer || maxCount <= 0)
        return B_BAD_VALUE;

    char linux_buf[1024];
    long nread = syscall(SYS_getdents64, fd, linux_buf, sizeof(linux_buf));
    if (nread <= 0)
        return (nread == 0) ? 0 : B_ERROR;

    int32 count = 0;
    long bpos = 0;
    char* out_ptr = (char*)buffer;
    size_t out_rem = bufferSize;

    while (bpos < nread && count < maxCount) {
        struct linux_dirent64* d = (struct linux_dirent64*)(linux_buf + bpos);
        size_t entry_size = sizeof(struct dirent);
        if (entry_size > out_rem)
            break;

        struct dirent* out_ent = (struct dirent*)out_ptr;
        out_ent->d_dev = 1;
        out_ent->d_pdev = 1;
        out_ent->d_ino = (ino_t)d->d_ino;
        out_ent->d_pino = 1;
        out_ent->d_reclen = (unsigned short)entry_size;
        strncpy(out_ent->d_name, d->d_name, sizeof(out_ent->d_name) - 1);
        out_ent->d_name[sizeof(out_ent->d_name) - 1] = '\0';

        out_ptr += entry_size;
        out_rem -= entry_size;
        count++;

        bpos += d->d_reclen;
    }

    return count;
}

status_t _kern_rewind_dir(int fd)
{
    return lseek(fd, 0, SEEK_SET) >= 0 ? B_OK : B_ERROR;
}

status_t _kern_close(int fd)
{
    return close(fd) == 0 ? B_OK : B_ERROR;
}

status_t _kern_entry_ref_to_path(dev_t device, ino_t node, const char *leaf,
    char *buffer, size_t bufferSize)
{
    if (!buffer || bufferSize == 0)
        return B_BAD_VALUE;

    char dirPath[B_PATH_NAME_LENGTH];
    if (lookup_dir_inode(node, dirPath, sizeof(dirPath))) {
        if (!leaf || leaf[0] == '\0' || strcmp(leaf, ".") == 0) {
            snprintf(buffer, bufferSize, "%s", dirPath);
        } else if (leaf[0] == '/') {
            snprintf(buffer, bufferSize, "%s", leaf);
        } else if (strcmp(dirPath, "/") == 0) {
            snprintf(buffer, bufferSize, "/%s", leaf);
        } else {
            snprintf(buffer, bufferSize, "%s/%s", dirPath, leaf);
        }
        return B_OK;
    }

    // An unknown directory inode cannot be reconstructed by guessing a path.
    return B_ENTRY_NOT_FOUND;
}

int _kern_open_query(dev_t device, const char *query, size_t queryLength,
    uint32 flags, port_id port, int32 token)
{
    return B_NOT_SUPPORTED;
}

void* load_driver_settings(const char *driverName)
{
    return nullptr;
}

void* load_driver_settings_file(int fd)
{
    return nullptr;
}

status_t unload_driver_settings(void *handle)
{
    return B_OK;
}

void* parse_driver_settings_string(const char *settingsString)
{
    return nullptr;
}

const driver_settings* get_driver_settings(void *handle)
{
    return nullptr;
}

time_t parsedate(const char *dateStr, time_t now)
{
    // Date-string parsing is not implemented by this adapter.
    return (time_t)-1;
}

// Only the root volume is enumerated by this adapter. Report the actual
// Linux device and filesystem statistics, including zero capacity on ramfs.
dev_t next_dev(int32 *pos)
{
    if (!pos || *pos < 0) return B_BAD_VALUE;
    if (*pos != 0) return B_ENTRY_NOT_FOUND;
    dev_t device = dev_for_path("/");
    if (device >= 0) ++*pos;
    return device;
}

dev_t dev_for_path(const char *path)
{
    if (!path) return B_BAD_VALUE;
    struct stat native;
    if (stat(path, &native) != 0) return B_ENTRY_NOT_FOUND;
    return (dev_t)native.st_dev;
}

int fs_stat_dev(dev_t dev, struct fs_info *info)
{
    if (!info) return B_BAD_VALUE;
    struct stat root;
    struct statvfs native;
    if (stat("/", &root) != 0 || statvfs("/", &native) != 0)
        return B_ERROR;
    if (dev != (dev_t)root.st_dev)
        return B_NOT_SUPPORTED;
    memset(info, 0, sizeof(*info));
    info->dev = dev;
    info->root = root.st_ino;
    if (native.f_flag & ST_RDONLY) info->flags |= B_FS_IS_READONLY;
    info->block_size = native.f_frsize ? native.f_frsize : native.f_bsize;
    info->io_size = native.f_bsize;
    info->total_blocks = native.f_blocks;
    info->free_blocks = native.f_bfree;
    info->total_nodes = native.f_files;
    info->free_nodes = native.f_ffree;
    snprintf(info->device_name, sizeof(info->device_name), "/");
    snprintf(info->volume_name, sizeof(info->volume_name), "/");
    FILE* mounts = setmntent("/proc/self/mounts", "r");
    if (mounts) {
        struct mntent entry;
        char buffer[4096];
        while (getmntent_r(mounts, &entry, buffer, sizeof(buffer))) {
            if (strcmp(entry.mnt_dir, "/") == 0) {
                snprintf(info->device_name, sizeof(info->device_name), "%s", entry.mnt_fsname);
                snprintf(info->fsh_name, sizeof(info->fsh_name), "%s", entry.mnt_type);
                break;
            }
        }
        endmntent(mounts);
    }
    // Unavailable names and capabilities remain unknown rather than invented.
    return B_OK;
}

status_t _kern_read_link(int fd, const char *path, char *buffer, size_t *bufferSize)
{
    if (!buffer || !bufferSize || *bufferSize == 0) return B_BAD_VALUE;
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    ssize_t ret = (path && path[0] != '\0')
        ? readlinkat(dirfd, path, buffer, *bufferSize - 1)
        : readlink("/proc/self/exe", buffer, *bufferSize - 1);
    if (ret < 0) return B_ERROR;
    buffer[ret] = '\0';
    *bufferSize = (size_t)ret;
    return B_OK;
}

status_t _kern_write_fs_info(dev_t device, const struct fs_info *info, int mask)
{
    return B_NOT_SUPPORTED;
}

mode_t __gUmask = 022;

int _kern_open(int fd, const char *path, int openMode, int perms)
{
    if (fd < 0 && (!path || path[0] == '\0')) return B_BAD_VALUE;
    const char *targetPath = (!path || path[0] == '\0') ? "." : path;
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    int flags = 0;
    if ((openMode & O_RWMASK) == O_RDONLY) flags |= O_RDONLY;
    else if ((openMode & O_RWMASK) == O_WRONLY) flags |= O_WRONLY;
    else if ((openMode & O_RWMASK) == O_RDWR) flags |= O_RDWR;
    if (openMode & O_CREAT) flags |= O_CREAT;
    if (openMode & O_TRUNC) flags |= O_TRUNC;
    if (openMode & O_APPEND) flags |= O_APPEND;
    if (openMode & O_EXCL) flags |= O_EXCL;
    if (openMode & O_CLOEXEC) flags |= O_CLOEXEC;
    int res = openat(dirfd, targetPath, flags, perms);
    return (res >= 0) ? res : B_ENTRY_NOT_FOUND;
}

int _kern_open_dir(int fd, const char *path)
{
    int res = _kern_open(fd, path, O_RDONLY | O_DIRECTORY, 0);
    if (res >= 0)
        record_fd_dir_inode(res);
    return res;
}

int _kern_open_parent_dir(int fd, char *name, size_t nameLength)
{
    char fullPath[B_PATH_NAME_LENGTH];
    if (get_fd_path(fd, fullPath, sizeof(fullPath))) {
        size_t len = strlen(fullPath);
        while (len > 1 && fullPath[len - 1] == '/') {
            fullPath[--len] = '\0';
        }
        if (strcmp(fullPath, "/") == 0) {
            if (name && nameLength > 0) {
                strncpy(name, ".", nameLength - 1);
                name[nameLength - 1] = '\0';
            }
            int res = _kern_open(fd, ".", O_RDONLY | O_DIRECTORY, 0);
            if (res >= 0)
                record_fd_dir_inode(res);
            return res;
        }
        const char* lastSlash = strrchr(fullPath, '/');
        if (lastSlash && name && nameLength > 0) {
            strncpy(name, lastSlash + 1, nameLength - 1);
            name[nameLength - 1] = '\0';
        }
    } else if (name && nameLength > 0) {
        name[0] = '\0';
    }
    int res = _kern_open(fd, "..", O_RDONLY | O_DIRECTORY, 0);
    if (res >= 0)
        record_fd_dir_inode(res);
    return res;
}

int _kern_open_entry_ref(dev_t device, ino_t inode, const char *name, int openMode, int perms)
{
    char dirPath[B_PATH_NAME_LENGTH];
    if (lookup_dir_inode(inode, dirPath, sizeof(dirPath))) {
        char fullPath[B_PATH_NAME_LENGTH];
        if (!name || name[0] == '\0' || strcmp(name, ".") == 0)
            snprintf(fullPath, sizeof(fullPath), "%s", dirPath);
        else if (name[0] == '/')
            snprintf(fullPath, sizeof(fullPath), "%s", name);
        else if (strcmp(dirPath, "/") == 0)
            snprintf(fullPath, sizeof(fullPath), "/%s", name);
        else
            snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, name);
        return _kern_open(-1, fullPath, openMode, perms);
    }
    return _kern_open(-1, name ? name : "", openMode, perms);
}

int _kern_open_dir_entry_ref(dev_t device, ino_t inode, const char *name)
{
    int res = _kern_open_entry_ref(device, inode, name, O_RDONLY | O_DIRECTORY, 0);
    if (res >= 0)
        record_fd_dir_inode(res);
    return res;
}

ssize_t _kern_read(int fd, off_t pos, void *buffer, size_t bufferSize)
{
    if (pos == -1) return read(fd, buffer, bufferSize);
    return pread(fd, buffer, bufferSize, pos);
}

ssize_t _kern_write(int fd, off_t pos, const void *buffer, size_t bufferSize)
{
    if (pos == -1) return write(fd, buffer, bufferSize);
    return pwrite(fd, buffer, bufferSize, pos);
}

off_t _kern_seek(int fd, off_t pos, int seekType)
{
    return lseek(fd, pos, seekType);
}

int _kern_dup(int fd)
{
    return dup(fd);
}

status_t _kern_read_stat(int fd, const char *path, bool traverseLink, struct stat *stat, size_t statSize)
{
    if (!stat) return B_BAD_VALUE;
    if (!path || path[0] == '\0') {
        if (fd < 0) return B_BAD_VALUE;
        int res = fstat(fd, stat);
        if (res == 0 && S_ISDIR(stat->st_mode))
            record_fd_dir_inode(fd);
        return (res == 0) ? B_OK : B_ENTRY_NOT_FOUND;
    }
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    int flags = traverseLink ? 0 : AT_SYMLINK_NOFOLLOW;
    int res = fstatat(dirfd, path, stat, flags);
    return (res == 0) ? B_OK : B_ENTRY_NOT_FOUND;
}

status_t _kern_write_stat(int fd, const char *path, bool traverseLink, const struct stat *stat, size_t statSize, int statMask)
{
    return B_OK;
}

status_t _kern_create_dir(int fd, const char *path, int perms)
{
    if (!path) return B_BAD_VALUE;
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    return mkdirat(dirfd, path, perms) == 0 ? B_OK : B_ERROR;
}

status_t _kern_remove_dir(int fd, const char *path)
{
    if (!path) return B_BAD_VALUE;
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    return unlinkat(dirfd, path, AT_REMOVEDIR) == 0 ? B_OK : B_ERROR;
}

status_t _kern_unlink(int fd, const char *path)
{
    if (!path) return B_BAD_VALUE;
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    return unlinkat(dirfd, path, 0) == 0 ? B_OK : B_ERROR;
}

status_t _kern_rename(int oldDir, const char *oldpath, int newDir, const char *newpath)
{
    if (!oldpath || !newpath) return B_BAD_VALUE;
    int dfd1 = (oldDir < 0) ? AT_FDCWD : oldDir;
    int dfd2 = (newDir < 0) ? AT_FDCWD : newDir;
    return renameat(dfd1, oldpath, dfd2, newpath) == 0 ? B_OK : B_ERROR;
}

status_t _kern_create_symlink(int fd, const char *path, const char *toPath, int mode)
{
    if (!path || !toPath) return B_BAD_VALUE;
    int dirfd = (fd < 0) ? AT_FDCWD : fd;
    return symlinkat(toPath, dirfd, path) == 0 ? B_OK : B_ERROR;
}

status_t _kern_fsync(int fd, bool dataOnly)
{
    return (dataOnly ? fdatasync(fd) : fsync(fd)) == 0 ? B_OK : B_ERROR;
}

status_t _kern_lock_node(int fd) { return flock(fd, LOCK_EX) == 0 ? B_OK : B_ERROR; }
status_t _kern_unlock_node(int fd) { return flock(fd, LOCK_UN) == 0 ? B_OK : B_ERROR; }
int _kern_open_attr_dir(int fd, const char *path, bool traverse) { return B_NOT_SUPPORTED; }
status_t _kern_remove_attr(int fd, const char *name) { return B_NOT_SUPPORTED; }
status_t _kern_rename_attr(int fromFD, const char *fromName, int toFD, const char *toName) { return B_NOT_SUPPORTED; }
status_t _kern_start_watching(dev_t device, ino_t node, uint32 flags, port_id port, int32 token) { return B_NOT_SUPPORTED; }
status_t _kern_stop_watching(dev_t device, ino_t node, port_id port, int32 token) { return B_NOT_SUPPORTED; }
status_t _kern_stop_notifying(port_id port, int32 token) { return B_NOT_SUPPORTED; }

status_t _get_next_image_info(team_id team, int32 *cookie, image_info *info, size_t size) { return B_NOT_SUPPORTED; }
size_t get_architectures(const char** architectures, size_t count) {
    if (architectures && count > 0) { architectures[0] = __HAIKU_ARCH_ABI; return 1; }
    return 1;
}
size_t get_secondary_architectures(const char** architectures, size_t count) { return 0; }
area_id _kern_transfer_area(area_id area, void** _address, uint32 addressSpec, team_id target) { return B_NOT_SUPPORTED; }
status_t fs_stat_index(dev_t device, const char *name, struct index_info *info) { return B_ENTRY_NOT_FOUND; }
thread_id load_image(int32 argc, const char **argv, const char **environ) { return B_NOT_SUPPORTED; }
status_t _kern_reserve_address_range(addr_t* _address, uint32 addressSpec, size_t size) { return B_NOT_SUPPORTED; }
status_t _kern_get_safemode_option(const char *parameter, char *buffer, size_t *_bufferSize) { return B_ENTRY_NOT_FOUND; }

} // extern "C"

void __initialize_locale_kit()
{
}

namespace BPrivate {
    port_id get_launch_daemon_port() { return B_NAME_NOT_FOUND; }
}
