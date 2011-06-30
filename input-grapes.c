/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <chunk.h>
#include <chunkiser.h>

#include "input.h"
#include "dbg.h"

extern int initial_id;

struct input_desc {
  struct input_stream *s;
  int id;
  int interframe;
  uint64_t start_time;
  uint64_t first_ts;
};

struct input_desc *input_open(const char *fname, int *fds, int fds_size)
{
  struct input_desc *res;
  struct timeval tv;
  char *c;

  res = malloc(sizeof(struct input_desc));
  if (res == NULL) {
    return NULL;
  }

  c = strchr(fname,',');
  if (c) {
    *(c++) = 0;
  }
  res->s = input_stream_open(fname, &res->interframe, c);
  if (res->s == NULL) {
    free(res);
    res = NULL;
    return res;
  }
  if (res->interframe == 0) {
    const int *my_fds;
    int i = 0;

    my_fds = input_get_fds(res->s);
    while(my_fds[i] != -1) {
      fds[i] = my_fds[i];
      i = i + 1;
    }
    fds[i] = -1;
  } else {
    if (fds_size >= 1) {
      fds[0] = -1; //This input module needs no fds to monitor
    }
    gettimeofday(&tv, NULL);
    res->start_time = tv.tv_usec + tv.tv_sec * 1000000ULL;
    res->first_ts = 0;
    res->id = 0; //(res->start_time / res->interframe) % INT_MAX; //TODO: verify 32/64 bit

    if(initial_id == -1) {
      res->id = (res->start_time / res->interframe) % INT_MAX; //TODO: verify 32/64 bit
    } else {
      res->id = initial_id;
    }

    fprintf(stderr,"Initial Chunk Id %d\n", res->id);
  }

  return res;
}

void input_close(struct input_desc *s)
{
  input_stream_close(s->s);
  free(s);
}

int input_get(struct input_desc *s, struct chunk *c)
{
  struct timeval now;
  int64_t delta;
  int res;

  c->attributes_size = 0;
  c->attributes = NULL;

  c->id = s->id;
  res = chunkise(s->s, c);
  if (res < 0) {
    return -1;
  }
  if (res > 0) {
    s->id++;
  }
  if (s->first_ts == 0) {
    s->first_ts = c->timestamp;
  }
  gettimeofday(&now, NULL);
  if (s->interframe) {
    delta = c->timestamp - s->first_ts + s->interframe;
    delta = delta + s->start_time - now.tv_sec * 1000000ULL - now.tv_usec;
    dprintf("Delta: %lld\n", delta);
    dprintf("Generate Chunk[%d] (TS: %llu)\n", c->id, c->timestamp);
    if (delta < 0) {
      delta = 0;
    }
  } else {
    delta = INT_MAX;
  }
  c->timestamp = now.tv_sec * 1000000ULL + now.tv_usec;

  return delta;
}