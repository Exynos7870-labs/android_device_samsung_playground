#ifndef _LOG_H_
#define _LOG_H_

#include <cerrno>
#include <cstring>

#include <log/log.h>

#ifndef ALOGERR
#define ALOGERR(fmt, args...) ((void)ALOG(LOG_ERROR, LOG_TAG, fmt " [%s]", ##args, strerror(errno)))
#endif

#endif // _LOG_H_
