/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "input-stream.h"

static struct input_stream {
} fake_descriptor;

struct input_stream *input_stream_open(const char *fname, int *period, uint16_t flags)
{
  *period = 40000;
  return &fake_descriptor;
}

void input_stream_close(struct input_stream *dummy)
{
}

uint8_t *chunkise(struct input_stream *dummy, int id, int *size, uint64_t *ts)
{
  uint8_t *res;
  const int header_size = 1 + 2 + 2 + 2 + 2 + 1; // 1 Frame type + 2 width + 2 height + 2 frame rate num + 2 frame rate den + 1 number of frames
  static char buff[80];

  sprintf(buff, "Chunk %d", id);
  *ts = 40 * id * 1000;
  *size = strlen(buff) + 1 + header_size + 2;
  res = malloc(*size);
  res[0] = 1;
  res[1] = 352 >> 8;
  res[2] = 352 & 0xFF;
  res[3] = 288 >> 8;
  res[4] = 288 & 0xFF;
  res[5] = 0;
  res[6] = 1;
  res[7] = 0;
  res[8] = 25;
  res[9] = 1;
  res[10] = (*size - header_size - 2) >> 8;
  res[11] = (*size - header_size - 2) & 0xFF;
  memcpy(res + header_size + 2, buff, *size - header_size - 2);

  return res;
}
