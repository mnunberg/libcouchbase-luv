#include "plugin.h"

int lcbuv_errno_map(int uverr)
{

#ifndef UNKNOWN
#define UNKNOWN -1
#endif

#ifndef EAIFAMNOSUPPORT
#define EAIFAMNOSUPPORT EAI_FAMILY
#endif

#ifndef EAISERVICE
#define EAISERVICE EAI_SERVICE
#endif

#ifndef EAI_SYSTEM
#define EAI_SYSTEM -11
#endif
#ifndef EADDRINFO
#define EADDRINFO EAI_SYSTEM
#endif

#ifndef EAISOCKTYPE
#define EAISOCKTYPE EAI_SOCKTYPE
#endif

#ifndef ECHARSET
#define ECHARSET 0
#endif

#ifndef EOF
#define EOF -1
#endif

#ifndef ENONET
#define ENONET ENETDOWN
#endif

#ifndef ESHUTDOWN
#define ESHUTDOWN WSAESHUTDOWN
#endif

#define OK 0

    int ret = 0;
#define X(errnum,errname,errdesc) \
    if (uverr == UV_##errname) { \
        return errname; \
    }
    UV_ERRNO_MAP(X);

    return ret;

#undef X
}
