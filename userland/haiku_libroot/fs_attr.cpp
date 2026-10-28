/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <fs_attr.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define ATTR_PREFIX "user.haiku."
#define ATTR_PREFIX_LEN 11

static void make_xattr_name(const char* attr, char* out, size_t out_len)
{
    snprintf(out, out_len, "%s%s", ATTR_PREFIX, attr);
}

extern "C" {

ssize_t fs_read_attr(int fd, const char *attribute, uint32 type,
                    off_t pos, void *buffer, size_t readBytes)
{
    if (!attribute || !buffer)
        return B_BAD_VALUE;

    char xattr_name[256];
    make_xattr_name(attribute, xattr_name, sizeof(xattr_name));

    ssize_t total = fgetxattr(fd, xattr_name, nullptr, 0);
    if (total < 0)
        return (errno == ENODATA || errno == ENOATTR) ? B_ENTRY_NOT_FOUND : B_ERROR;

    if (pos >= total)
        return 0;

    char* temp = new char[total];
    ssize_t ret = fgetxattr(fd, xattr_name, temp, total);
    if (ret > 0) {
        size_t available = (size_t)(ret - pos);
        size_t to_copy = (readBytes < available) ? readBytes : available;
        memcpy(buffer, temp + pos, to_copy);
        delete[] temp;
        return to_copy;
    }
    delete[] temp;
    return B_ERROR;
}

ssize_t fs_write_attr(int fd, const char *attribute, uint32 type,
                     off_t pos, const void *buffer, size_t writeBytes)
{
    if (!attribute || !buffer)
        return B_BAD_VALUE;

    char xattr_name[256];
    make_xattr_name(attribute, xattr_name, sizeof(xattr_name));

    int rc = fsetxattr(fd, xattr_name, buffer, writeBytes, 0);
    if (rc == 0)
        return writeBytes;
    return B_ERROR;
}

int fs_remove_attr(int fd, const char *attribute)
{
    if (!attribute)
        return B_BAD_VALUE;

    char xattr_name[256];
    make_xattr_name(attribute, xattr_name, sizeof(xattr_name));

    if (fremovexattr(fd, xattr_name) == 0)
        return B_OK;
    return B_ENTRY_NOT_FOUND;
}

int fs_stat_attr(int fd, const char *attribute, struct attr_info *attrInfo)
{
    if (!attribute || !attrInfo)
        return B_BAD_VALUE;

    char xattr_name[256];
    make_xattr_name(attribute, xattr_name, sizeof(xattr_name));

    ssize_t sz = fgetxattr(fd, xattr_name, nullptr, 0);
    if (sz >= 0) {
        attrInfo->type = 0;
        attrInfo->size = sz;
        return B_OK;
    }
    return B_ENTRY_NOT_FOUND;
}

int fs_open_attr(const char *path, const char *attribute, uint32 type, int openMode) { return B_ERROR; }
int fs_fopen_attr(int fd, const char *attribute, uint32 type, int openMode) { return B_ERROR; }
int fs_close_attr(int fd) { return B_OK; }
DIR *fs_open_attr_dir(const char *path) { return nullptr; }
DIR *fs_lopen_attr_dir(const char *path) { return nullptr; }
DIR *fs_fopen_attr_dir(int fd) { return nullptr; }
int fs_close_attr_dir(DIR *dir) { return B_OK; }
struct dirent *fs_read_attr_dir(DIR *dir) { return nullptr; }
void fs_rewind_attr_dir(DIR *dir) {}

typedef struct {
    time_t tv_sec;
} stat_beos_time;

struct stat_beos {
    dev_t          st_dev;
    ino_t          st_ino;
    mode_t         st_mode;
    nlink_t        st_nlink;
    uid_t          st_uid;
    gid_t          st_gid;
    off_t          st_size;
    dev_t          st_rdev;
    blksize_t      st_blksize;
    stat_beos_time st_atim;
    stat_beos_time st_mtim;
    stat_beos_time st_ctim;
    stat_beos_time st_crtim;
};

void convert_to_stat_beos(const struct stat* stat, struct stat_beos* beosStat)
{
    if (!stat || !beosStat) return;
    beosStat->st_dev = stat->st_dev;
    beosStat->st_ino = stat->st_ino;
    beosStat->st_mode = stat->st_mode;
    beosStat->st_nlink = stat->st_nlink;
    beosStat->st_uid = stat->st_uid;
    beosStat->st_gid = stat->st_gid;
    beosStat->st_size = stat->st_size;
    beosStat->st_rdev = stat->st_rdev;
    beosStat->st_blksize = stat->st_blksize;
    beosStat->st_atim.tv_sec = stat->st_atim.tv_sec;
    beosStat->st_mtim.tv_sec = stat->st_mtim.tv_sec;
    beosStat->st_ctim.tv_sec = stat->st_ctim.tv_sec;
    beosStat->st_crtim.tv_sec = stat->st_ctim.tv_sec;
}

void convert_from_stat_beos(const struct stat_beos* beosStat, struct stat* stat)
{
    if (!stat || !beosStat) return;
    stat->st_dev = beosStat->st_dev;
    stat->st_ino = beosStat->st_ino;
    stat->st_mode = beosStat->st_mode;
    stat->st_nlink = beosStat->st_nlink;
    stat->st_uid = beosStat->st_uid;
    stat->st_gid = beosStat->st_gid;
    stat->st_size = beosStat->st_size;
    stat->st_rdev = beosStat->st_rdev;
    stat->st_blksize = beosStat->st_blksize;
    stat->st_atim.tv_sec = beosStat->st_atim.tv_sec;
    stat->st_mtim.tv_sec = beosStat->st_mtim.tv_sec;
    stat->st_ctim.tv_sec = beosStat->st_ctim.tv_sec;
}

}
