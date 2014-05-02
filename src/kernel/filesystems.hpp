#ifndef _FILESYSTEMS_HPP
#define _FILESYSTEMS_HPP

#include "fs_interface.hpp"

void fs_init();
bool fs_mount(char *name, char *device, char *fs);
bool fs_unmount(char *name);
file_handle fs_open(char *path);
bool fs_close(file_handle &file);
int fs_read(file_handle &file, size_t bytes, char *buf);
bool fs_write(file_handle &file, size_t bytes, char *buf);
bool fs_seek(file_handle &file, int32_t pos, bool relative);
int fs_ioctl(file_handle &file, int fn, size_t bytes, char *buf);
dir_handle fs_open_dir(char *path);
bool fs_close_dir(dir_handle &dir);
directory_entry fs_read_dir(dir_handle &dir);
bool fs_write_dir(dir_handle &dir, directory_entry entry);
bool fs_seek_dir(dir_handle &dir, size_t pos, bool relative);
directory_entry fs_stat(char *path);

#endif