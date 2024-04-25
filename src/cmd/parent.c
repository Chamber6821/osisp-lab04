#define _GNU_SOURCE

#include "io.h"
#include "message.h"
#include "ring.h"
#include "shared.h"

#include <alloca.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

struct Shared *shared;

volatile int running = 1;

void stop() { running = 0; }

struct Shared {
  pthread_mutex_t general;
  int sendCount;
  int readCount;
  struct Ring *ring;
} *shared = NULL;

void initShared(int ringCapacity) {
  shared = smalloc(sizeof(struct Shared) + sizeof(struct Ring) + ringCapacity);
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&shared->general, &attr);
  shared->sendCount = 0;
  shared->readCount = 0;
  shared->ring = (struct Ring *)(((char *)shared) + sizeof(struct Shared));
  Ring_construct(shared->ring, ringCapacity);
}

void destroyShared() {
  Ring_desctruct(shared->ring);
  pthread_mutex_destroy(&shared->general);
  sfree(shared);
}

void producer() {
  printf("Producer %6d Started\n", getpid());
  while (running) {
    sleep(1);
    pthread_mutex_lock(&shared->general);
    char bytes[MESSAGE_MAX_SIZE] = {0};
    struct Message *message = Message_constructRandom((struct Message *)bytes);
    while (running) {
      pthread_mutex_unlock(&shared->general);
      pthread_mutex_lock(&shared->general);
      if (Message_sendTo(message, shared->ring) == -1) continue;
      shared->sendCount++;
      char data[255 * 3] = {0};
      bytes2hex(data, message->size, message->data);
      printf(
          "Producer %6d Sent %04hX:%04hX       %.80s\n",
          getpid(),
          message->type,
          message->hash,
          data
      );
      break;
    }
    pthread_mutex_unlock(&shared->general);
  }
}

void consumer() {
  printf("Consumer %6d Started\n", getpid());
  while (running) {
    pthread_mutex_lock(&shared->general);
    while (running) {
      pthread_mutex_unlock(&shared->general);
      pthread_mutex_lock(&shared->general);
      char bytes[MESSAGE_MAX_SIZE] = {0};
      struct Message *message =
          Message_readFrom((struct Message *)bytes, shared->ring);
      if (!message) continue;
      shared->readCount++;
      char data[255 * 3] = {0};
      bytes2hex(data, message->size, message->data);
      printf(
          "Consumer %6d Got  %04hX:%04hX(%04hX) %.80s\n",
          getpid(),
          message->type,
          message->hash,
          Message_hash(message),
          data
      );
      break;
    }
    pthread_mutex_unlock(&shared->general);
    sleep(1);
  }
}

pid_t run(void (*worker)()) {
  pid_t pid = fork();
  if (pid) return pid;
  signal(SIGUSR1, stop);
  worker();
  exit(0);
}

int producerCount = 0;
pid_t *producers = NULL;

int consumerCount = 0;
pid_t *consumers = NULL;

typedef int (*handle_f)();

int showInfo() {
  printf(
      "Sent %d(%d) Got %d(%d)\n",
      shared->sendCount,
      producerCount,
      shared->readCount,
      consumerCount
  );
  return 0;
}

int addProducer() {
  pid_t pid = run(producer);
  producerCount++;
  producers = realloc(producers, sizeof(*producers) * producerCount);
  producers[producerCount - 1] = pid;
  return 0;
}

int killProducer() {
  if (producerCount == 0) return 0;
  producerCount--;
  printf("Kill producer %6d\n", producers[producerCount]);
  kill(producers[producerCount], SIGUSR1);
  waitpid(producers[producerCount], NULL, 0);
  return 0;
}

int addConsumer() {
  pid_t pid = run(consumer);
  consumerCount++;
  consumers = realloc(consumers, sizeof(*consumers) * consumerCount);
  consumers[consumerCount - 1] = pid;
  return 0;
}

int killConsumer() {
  if (consumerCount == 0) return 0;
  consumerCount--;
  printf("Kill consumer %6d\n", consumers[consumerCount]);
  kill(consumers[consumerCount], SIGUSR1);
  waitpid(consumers[consumerCount], NULL, 0);
  return 0;
}

int quit() { return -1; }

int unknownCommand() { return 0; }

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
  initShared(1024);
  while (handleFor(getch())() == 0)
    ;
  while (producerCount)
    killProducer();
  while (consumerCount)
    killConsumer();
  destroyShared();
}
