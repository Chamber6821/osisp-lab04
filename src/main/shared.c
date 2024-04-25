#include "shared.h"
#include <stddef.h>
#include <sys/mman.h>

void *smalloc(int size) {
  void *block = mmap(
      NULL,
      size + sizeof(size),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANONYMOUS,
      -1,
      0
  );
  *((int *)block) = size;
  return (char *)block + sizeof(size);
}

void sfree(void *shared) {
  void *block = (char *)shared - sizeof(int);
  munmap(block, *((int *)block));
}
