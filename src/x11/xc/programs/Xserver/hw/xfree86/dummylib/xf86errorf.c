/* $XFree86: xc/programs/Xserver/hw/xfree86/dummylib/xf86errorf.c,v 1.3 2003/09/09 03:20:38 dawes Exp $ */

#include "X.h"
#include "os.h"
#include "xf86.h"
#include "xf86Priv.h"

/*
 * Utility functions required by libxf86_os. 
 */

void
xf86ErrorF(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    LogVWrite(1, format, ap);
    va_end(ap);
}

void
ErrorF(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    LogVWrite(1, format, ap);
    va_end(ap);
}
