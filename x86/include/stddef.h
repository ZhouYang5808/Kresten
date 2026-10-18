#ifndef _STDDEF_H
#define _STDDEF_H

#define NULL ((void *)0)
typedef long unsigned int size_t;
typedef long int ptrdiff_t;
typedef struct { char _[16]; } max_align_t;
#ifdef __cplusplus
typedef decltype(nullptr) nullptr_t;
#else
typedef void* nullptr_t;
#endif

#endif
