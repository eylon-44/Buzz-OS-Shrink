// sys/stat.h // ~ eylon

#if !defined(__LIBC_STAT_H)
#define __LIBC_STAT_H

#include <stddef.h>
#include <dirent.h>

struct stat {
    size_t st_size;     // size of file in bytes
    unsigned char type; // file type
    int indx;           // inode index
};

int stat(const char* pathname, struct stat* statbuf);
int fstat(int fd, struct stat* statbuf);
int mkdir(const char* pathname);

#endif