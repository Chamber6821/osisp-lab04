#include "ring.h"

struct Ring *Ring_construct(struct Ring *this, int capacity) {
  *this = (struct Ring){.capacity = capacity, .begin = 0, .end = 0};
  return this;
}

void Ring_desctruct(struct Ring *this) { (void)this; }

int Ring_length(struct Ring *this) {
  return this->begin <= this->end
             ? this->end - this->begin
             : ((this->end - 0) + (this->capacity - this->begin));
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
  if (Ring_alloc(this, length) == -1) return -1;
  int base = this->end - length + this->capacity;
  for (int i = 0; i < length; i++) {
    *Ring_byte(this, base + i) = bytes[i];
  }
  return 0;
}

int Ring_read(struct Ring *this, int length, char bytes[]) {
  if (Ring_length(this) < length) return -1;
  int base = this->begin;
  for (int i = 0; i < length; i++) {
    bytes[i] = *Ring_byte(this, base + i);
  }
  Ring_free(this, length);
  return 0;
}
