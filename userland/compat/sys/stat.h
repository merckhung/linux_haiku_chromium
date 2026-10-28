/*
 * Copyright 2002-2012 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _COMPAT_SYS_STAT_H_
#define _COMPAT_SYS_STAT_H_

#include <sys/stat.h>

typedef struct {
	time_t	tv_sec;
} stat_beos_time;

struct stat_beos {
	dev_t			st_dev;
	ino_t			st_ino;
	mode_t			st_mode;
	nlink_t			st_nlink;
	uid_t			st_uid;
	gid_t			st_gid;
	off_t			st_size;
	dev_t			st_rdev;
	blksize_t		st_blksize;
	stat_beos_time	st_atim;
	stat_beos_time	st_mtim;
	stat_beos_time	st_ctim;
	stat_beos_time	st_crtim;
};

#ifdef __cplusplus
extern "C" {
#endif

void convert_to_stat_beos(const struct stat* stat, struct stat_beos* beosStat);
void convert_from_stat_beos(const struct stat_beos* beosStat, struct stat* stat);

#ifdef __cplusplus
}
#endif

#endif	/* _COMPAT_SYS_STAT_H_ */
