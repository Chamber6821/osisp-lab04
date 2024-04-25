#pragma once

struct Ring {
  int capacity;
  int begin;
  int end;
  char data[];
};

struct Ring *Ring_construct(struct Ring *this, int capacity);
void Ring_desctruct(struct Ring *this);
int Ring_length(struct Ring *this);
int Ring_available(struct Ring *this);
int Ring_alloc(struct Ring *this, int size);
int Ring_free(struct Ring *this, int size);
char *Ring_byte(struct Ring *this, int index);
int Ring_send(struct Ring *this, int length, char bytes[]);
int Ring_read(struct Ring *this, int length, char bytes[]);
