#include "memcmp_wrapper.h"

int my_memcmp(const void *str1, const void *str2, size_t n) {
   const unsigned char *ptr1 = (const unsigned char*)str1;
   const unsigned char *ptr2 = (const unsigned char*)str2;

   for (size_t i = 0; i < n; i++) {
      if (ptr1[i] != ptr2[i]) {
         return ptr1[i] - ptr2[i];
      }
   }
   return 0;
}

int memcmp(const void *s1, const void *s2, size_t n) {
   return my_memcmp(s1, s2, n);
}