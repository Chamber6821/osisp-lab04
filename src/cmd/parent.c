#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct Ring {
  pthread_mutex_t send;
  pthread_mutex_t read;
  pthread_mutex_t general;
  int capacity;
  int begin;
  int end;
  char data[];
};

struct Ring *Ring_construct(struct Ring *this, int capacity) {
  *this = (struct Ring
  ){.send = PTHREAD_MUTEX_INITIALIZER,
    .read = PTHREAD_MUTEX_INITIALIZER,
    .general = PTHREAD_MUTEX_INITIALIZER,
    .capacity = capacity,
    .begin = 0,
    .end = 0};
  return this;
}

void Ring_desctruct(struct Ring *this) {
  pthread_mutex_destroy(&this->send);
  pthread_mutex_destroy(&this->read);
  pthread_mutex_destroy(&this->general);
}

int Ring_length(struct Ring *this) {
  pthread_mutex_lock(&this->general);
  int length = this->begin <= this->end
                   ? this->end - this->begin
                   : ((this->end - 0) + (this->capacity - this->begin));
  pthread_mutex_unlock(&this->general);
  return length;
}

int Ring_available(struct Ring *this) {
  return this->capacity - 1 - Ring_length(this);
}

int Ring_alloc(struct Ring *this, int size) {
  if (size < 0) return -1;
  if (Ring_available(this) < size) return -1;
  this->end = (this->end + size) % this->capacity;
  return 0;
}

int Ring_free(struct Ring *this, int size) {
  if (size < 0) return -1;
  if (Ring_length(this) < size) return -1;
  this->begin = (this->begin + size) % this->capacity;
  return 0;
}

char *Ring_byte(struct Ring *this, int index) {
  return &(this->data[index % this->capacity]);
}

int Ring_send(struct Ring *this, int length, char bytes[]) {
  if (length >= this->capacity) return -1;
  pthread_mutex_lock(&this->send);
  int base = this->end;
  while (Ring_alloc(this, length) == -1)
    ;
  for (int i = 0; i < length; i++) {
    *Ring_byte(this, base + i) = bytes[i];
  }
  pthread_mutex_unlock(&this->send);
  return 0;
}

int Ring_read(struct Ring *this, int length, char bytes[]) {
  pthread_mutex_lock(&this->read);
  int base = this->begin;
  while (Ring_length(this) < length)
    ;
  for (int i = 0; i < length; i++) {
    bytes[i] = *Ring_byte(this, base + i);
  }
  Ring_free(this, length);
  pthread_mutex_unlock(&this->read);
  return 0;
}

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

int main() {
  char buffer[80];
  struct Ring *ring = Ring_construct(malloc(sizeof(struct Ring) + 42), 42);
  for (int i = 0; i < 10000; i++) {
    sprintf(buffer, "%d", i);
    Ring_send(ring, strlen(buffer) + 1, buffer);
    printf(
        "%s | cap:%2d beg:%2d end:%2d\n",
        buffer,
        ring->capacity,
        ring->begin,
        ring->end
    );
    Ring_read(ring, strlen(buffer) + 1, buffer);
    printf(
        "%s | cap:%2d beg:%2d end:%2d\n",
        buffer,
        ring->capacity,
        ring->begin,
        ring->end
    );
  }
  Ring_desctruct(ring);
}
