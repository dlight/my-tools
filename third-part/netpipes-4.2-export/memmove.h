
#ifdef NO_MEMMOVE
void *memmove(/*void*, void*, size_t*/);

#else

/* rely on the system */
#include "string.h"

#endif
