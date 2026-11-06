#ifndef _BLACKWHITELIST_H
#define _BLACKWHITELIST_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int blackwhitelist_load_list(const char *filename);
int blackwhitelist_check_hostname(const char *host_addr, size_t host_len);

#ifdef __cplusplus
}
#endif
#endif
