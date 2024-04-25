#pragma once

#include "ring.h"
#include <stdint.h>

struct Message {
  uint8_t type;
  uint8_t size;
  uint16_t hash;
  char data[];
};

#define MESSAGE_MAX_SIZE (sizeof(struct Message) + 255)

struct Message *Message_constructRandom(struct Message *this);
struct Message *Message_readFrom(struct Message *this, struct Ring *ring);
uint16_t Message_hash(struct Message *this);
int Message_size(struct Message *this);
int Message_sendTo(struct Message *this, struct Ring *to);
