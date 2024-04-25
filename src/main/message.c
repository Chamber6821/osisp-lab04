#include "message.h"
#include "ring.h"
#include <stdlib.h>

struct Message *Message_constructRandom(struct Message *this) {
  *this = (struct Message){.type = rand(), .hash = 0, .size = rand()};
  for (int i = 0; i < this->size; i++)
    this->data[i] = rand();
  this->hash = Message_hash(this);
  return this;
}

struct Message *Message_readFrom(struct Message *this, struct Ring *ring) {
  if (Ring_read(ring, sizeof(struct Message), (char *)this) == -1) return NULL;
  Ring_read(ring, this->size, this->data);
  return this;
}

static uint16_t _xor(int length, char bytes[]) {
  uint16_t acc = 0;
  for (int i = 0; i < length; i++)
    acc ^= bytes[i];
  return acc;
}

uint16_t Message_hash(struct Message *this) {
  uint16_t old = this->hash;
  this->hash = 0;
  uint16_t calculated = _xor(Message_size(this), (char *)this);
  this->hash = old;
  return calculated;
}

int Message_size(struct Message *this) { return sizeof(*this) + this->size; }

int Message_sendTo(struct Message *this, struct Ring *to) {
  return Ring_send(to, Message_size(this), (char *)this);
}
