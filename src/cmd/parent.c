#define _GNU_SOURCE

#include <alloca.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
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
  ){.send = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP,
    .read = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP,
    .general = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP,
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
  while (Ring_available(this) < length)
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

struct Message {
  uint8_t type;
  uint16_t hash;
  uint8_t size;
  char data[];
};

#define MESSAGE_MAX_SIZE (sizeof(struct Message) + 255)

int Message_size(struct Message *this) { return sizeof(*this) + this->size; }

uint16_t _xor(int length, char bytes[]) {
  uint16_t acc = 0;
  for (int i = 0; i < length; i++) {
    acc ^= bytes[i];
  }
  return acc;
}

struct Message *newRandomMessage() {
  uint8_t size = rand();
  struct Message *message = malloc(sizeof(struct Message) + size);
  *message = (struct Message){.type = rand(), .hash = 0, .size = size};
  for (int i = 0; i < size; i++) {
    message->data[i] = rand();
  }
  message->hash = _xor(sizeof(struct Message) + size, (char *)message);
  return message;
}

struct Message *readMessage(struct Ring *ring) {
  struct Message *head = alloca(MESSAGE_MAX_SIZE);
  pthread_mutex_lock(&ring->read);
  Ring_read(ring, sizeof(struct Message), (char *)head);
  Ring_read(ring, head->size, head->data);
  pthread_mutex_unlock(&ring->read);
  return memcpy(malloc(Message_size(head)), head, Message_size(head));
}

void producer(struct Ring *buffer) {
  printf("Producer %5d Started\n", getpid());
  while (1) {
    struct Message *message = newRandomMessage();
    Ring_send(buffer, Message_size(message), (char *)message);
    printf(
        "Producer %5d Sent message with type %02hX and hash %04hX Buffer length %d\n",
        getpid(),
        message->type,
        message->hash,
        Ring_length(buffer)
    );
    free(message);
    sleep(1);
  }
}

void consumer(struct Ring *buffer) {
  printf("Consumer %5d Started\n", getpid());
  while (1) {
    struct Message *message = readMessage(buffer);
    printf(
        "Consumer %5d Got message with type %02hX and hash %04hX\n",
        getpid(),
        message->type,
        message->hash
    );
    free(message);
    sleep(1);
  }
}

int run(void (*worker)(struct Ring *buffer), struct Ring *buffer) {
  int pid = fork();
  if (pid) return pid;
  worker(buffer);
  exit(0);
}

int main() {
  int ringCapacity = 1024;
  struct Ring *ring =
      Ring_construct(malloc(sizeof(struct Ring) + ringCapacity), ringCapacity);
  int pp = run(producer, ring);
  int cp = run(consumer, ring);
  sleep(10);
  kill(cp, SIGKILL);
  kill(pp, SIGKILL);
  Ring_desctruct(ring);
}
