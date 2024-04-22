#define _GNU_SOURCE

#include <alloca.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <termios.h>
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
  *this = (struct Ring){.capacity = capacity, .begin = 0, .end = 0};
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&this->send, &attr);
  pthread_mutex_init(&this->read, &attr);
  pthread_mutex_init(&this->general, &attr);
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

uint16_t Message_hash(struct Message *this) {
  uint16_t old = this->hash;
  this->hash = 0;
  uint16_t calculated = _xor(Message_size(this), (char *)this);
  this->hash = old;
  return calculated;
}

struct Message *newRandomMessage() {
  uint8_t size = rand();
  struct Message *message = malloc(sizeof(struct Message) + size);
  *message = (struct Message){.type = rand(), .hash = 0, .size = size};
  for (int i = 0; i < size; i++) {
    message->data[i] = rand();
  }
  message->hash = Message_hash(message);
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
        "Producer %5d Sent message with hash %04hX Buffer length %d\n",
        getpid(),
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
        "Consumer %5d Got  message with hash %04hX Expected hash %04hX Buffer length %d\n",
        getpid(),
        message->hash,
        Message_hash(message),
        Ring_length(buffer)
    );
    free(message);
    sleep(1);
  }
}

pid_t run(void (*worker)(struct Ring *buffer), struct Ring *buffer) {
  pid_t pid = fork();
  if (pid) return pid;
  worker(buffer);
  exit(0);
}

int getch() {
  struct termios old, current;
  tcgetattr(STDIN_FILENO, &current);
  old = current;
  current.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSANOW, &current);
  int ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  return ch;
}

int producerCount = 0;
pid_t *producers = NULL;

int consumerCount = 0;
pid_t *consumers = NULL;

typedef int (*handle_f)(struct Ring *ring);

int showInfo(struct Ring *ring) {
  (void)ring;
  printf("Producers %d Consumers %d\n", producerCount, consumerCount);
  return 0;
}

int addProducer(struct Ring *ring) {
  pid_t pid = run(producer, ring);
  producerCount++;
  producers = realloc(producers, sizeof(*producers) * producerCount);
  producers[producerCount - 1] = pid;
  return 0;
}

int killProducer(struct Ring *ring) {
  (void)ring;
  if (producerCount == 0) return 0;
  producerCount--;
  printf("Kill producer %5d\n", producers[producerCount]);
  kill(producers[producerCount], SIGKILL);
  waitpid(producers[producerCount], NULL, 0);
  return 0;
}

int addConsumer(struct Ring *ring) {
  pid_t pid = run(consumer, ring);
  consumerCount++;
  consumers = realloc(consumers, sizeof(*consumers) * consumerCount);
  consumers[consumerCount - 1] = pid;
  return 0;
}

int killConsumer(struct Ring *ring) {
  (void)ring;
  if (consumerCount == 0) return 0;
  consumerCount--;
  printf("Kill consumer %5d\n", consumers[consumerCount]);
  kill(consumers[consumerCount], SIGKILL);
  waitpid(consumers[consumerCount], NULL, 0);
  return 0;
}

int quit(struct Ring *ring) {
  (void)ring;
  return -1;
}

int unknownCommand(struct Ring *ring) {
  (void)ring;
  return 0;
}

handle_f handleFor(char key) {
  switch (key) {
  case 'i': return showInfo;
  case 'p': return addProducer;
  case 'P': return killProducer;
  case 'c': return addConsumer;
  case 'C': return killConsumer;
  case 'q': return quit;
  default: return unknownCommand;
  }
}

int main() {
  int ringCapacity = 1024;
  struct Ring *ring =
      Ring_construct(smalloc(sizeof(struct Ring) + ringCapacity), ringCapacity);
  while (handleFor(getch())(ring) == 0)
    ;
  while (producerCount)
    killProducer(ring);
  while (consumerCount)
    killConsumer(ring);
  Ring_desctruct(ring);
  sfree(ring);
}
