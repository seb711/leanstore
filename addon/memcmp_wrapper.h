//
// Created by kosakseb on 10/28/24.
//

#ifndef LEANSTOREDB_MY_MEMCMP_H
#define LEANSTOREDB_MY_MEMCMP_H

#include <stddef.h>  // For size_t

#ifdef __cplusplus
extern "C" {
#endif

// Custom memcmp function without noexcept
int my_memcmp(const void *str1, const void *str2, size_t n);

#ifdef __cplusplus
}
#endif

#endif  // LEANSTOREDB_MY_MEMCMP_H
