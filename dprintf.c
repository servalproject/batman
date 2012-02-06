#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

void dprintf(int fd,const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdprintf(fd,fmt,ap);
    va_end(ap);
}

void vdprintf(int fd,const char *fmt,va_list ap)
{
    char buff[8192];
    int len=vsnprintf(buff,8192,fmt,ap);
    if (len>-1) write(fd,buff,len);
}
