#ifndef _BACKEND_HPP
#define _BACKEND_HPP
#include "module_api.h"
#include "terminal.h"

class i_backend{
public:
    virtual size_t display_read(size_t bytes, char *buf)=0;
    virtual size_t display_write(size_t bytes, char *buf)=0;
    virtual size_t display_seek(size_t pos, bool relative)=0;
    virtual int display_ioctl(int fn, size_t bytes, char *buf)=0;

    virtual size_t input_read(size_t bytes, char *buf)=0;
    virtual size_t input_write(size_t bytes, char *buf)=0;
    virtual size_t input_seek(size_t pos, bool relative)=0;
    virtual int input_ioctl(int fn, size_t bytes, char *buf)=0;

    virtual bt_terminal_mouse_info mouse_read()=0;
    virtual void show_mouse()=0;
    virtual void hide_mouse()=0;
    virtual bool get_mouse_visibility()=0;
    virtual void set_mouse_bitmap(bt_terminal_mouse_bitmap bmp)=0;

    virtual bool is_active(uint64_t id)=0;
    virtual void set_active(uint64_t id)=0;

    virtual void open(uint64_t id)=0;
    virtual void close(uint64_t id)=0;

    virtual ~i_backend(){};
};

#endif