#ifndef __XF_LOG_H__
#define __XF_LOG_H__

#include <stdio.h>

#include "xf_log_config.h"

#ifndef XF_LOG_ENABLE
#error "xf_log_config.h must define XF_LOG_ENABLE"
#endif

#if XF_LOG_ENABLE == 1
#define LOG_NR(format, ...) \
do { \
    printf(format, ##__VA_ARGS__); \
} while (0)

#define LOG_N(format, ...) \
do { \
    printf(format, ##__VA_ARGS__); \
    printf("\r\n"); \
} while (0)

#define LOG_I(format, ...) \
do { \
    printf("%15s-%04d | " format "\r\n", \
            LOCAL_TAG, __LINE__, ##__VA_ARGS__ ); \
} while (0)

#define LOG_E(format, ...) \
do { \
    printf("%15s-%04d | %s %s: " format "\r\n", \
            LOCAL_TAG, __LINE__, __FILE__,  __func__, ##__VA_ARGS__ ); \
} while (0)
#else
#define LOG_NR(format, ...) do { } while (0)
#define LOG_N(format, ...) do { } while (0)
#define LOG_I(format, ...) do { } while (0)
#define LOG_E(format, ...) do { } while (0)
#endif

#endif
