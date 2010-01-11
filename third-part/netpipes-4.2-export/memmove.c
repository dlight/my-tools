
#include <sys/types.h>

#ifdef NO_MEMMOVE
/* bloody SunOS, it's not ANSI */
void *memmove(dst, src, n)
    void *dst;
    void *src;
    size_t n;
{
    int	i;
    /* this memmove is not as functional as the ANSI one, it only works for
       what we do in copyio */
    for (i=0; i<n; i++) {
	((char*)dst)[i] = ((char*)src)[i];
    }
    return dst;
}

#endif
