#ifndef UPRO_MEM_H
#define UPRO_MEM_H


#if ((__GNUC__ * 100 + __GNUC__MINOR__) > 430)  /* gcc version > 4.3 */
# define ALLOCSZ_ATTR(x,...) __attribute__ ((alloc_size(x, ##__VA_ARGS__)))
#else
# define ALLOCSZ_ATTR(x,...)
#endif

void *upro_mem_malloc(const size_t size);
void *upro_mem_calloc(const size_t size);
void *upro_mem_realloc(void *ptr, const size_t size);
void upro_mem_free(void *ptr);

#endif
