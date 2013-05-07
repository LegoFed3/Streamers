/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#include <chunk.h>
#include <chunkiser.h>

#include "output.h"
#include "measures.h"
#include "dbg.h"

int fixed_playout_delay=0;
struct timeval period,tconsume,tcnt;

static int last_chunk = -1;
static int next_chunk = -1;
static int buff_size;
extern bool chunk_log;
extern int start_id;
extern int end_id;

static char sflag = 0;
static char eflag = 0;

bool reorder = OUTPUT_REORDER;

struct outbuf {
  struct chunk c;
};
static struct outbuf *buff;
static struct output_stream *out;

void output_init(int bufsize, const char *config)
{
  char *c;

  c = strchr(config,',');
  if (c) {
    *(c++) = 0;
  }
  out = out_stream_init(config, c);
  if (out == NULL) {
     fprintf(stderr, "Error: can't initialize output module\n");
     exit(1);
  }
  if (!buff) {
    int i;

    buff_size = bufsize;
    buff = malloc(sizeof(struct outbuf) * buff_size);
    if (!buff) {
     fprintf(stderr, "Error: can't allocate output buffer\n");
     exit(1);
    }
    for (i = 0; i < buff_size; i++) {
      buff[i].c.data = NULL;
    }
  } else {
   fprintf(stderr, "Error: output buffer re-init not allowed!\n");
   exit(1);
  }
}

static void buffer_print(void)
{
#ifdef DEBUG
  int i;

  if (next_chunk < 0) {
    return;
  }

  dprintf("\toutbuf: %d-> ",next_chunk);
  for (i = next_chunk; i < next_chunk + buff_size; i++) {
    if (buff[i % buff_size].c.data) {
      dprintf("%d",i % 10);
    } else {
      dprintf(".");
    }
  }
  dprintf("\n");
#endif
}

static void buffer_free(int i)
{
  dprintf("\t\tFlush Buf %d: %s\n", i, buff[i].c.data);
  if(start_id == -1 || buff[i].c.id >= start_id) {
    if(end_id == -1 || buff[i].c.id <= end_id) {
      if(sflag == 0) {
        fprintf(stderr, "\nFirst chunk id played out: %d\n\n",buff[i].c.id);
        sflag = 1;
      }
      if (reorder) chunk_write(out, &buff[i].c);
      last_chunk = buff[i].c.id;
    } else if (eflag == 0 && last_chunk != -1) {
      fprintf(stderr, "\nLast chunk id played out: %d\n\n", last_chunk);
      eflag = 1;
    }
  }

  free(buff[i].c.data);
  buff[i].c.data = NULL;
  dprintf("Next Chunk: %d -> %d\n", next_chunk, buff[i].c.id + 1);
  reg_chunk_playout(buff[i].c.id, true, buff[i].c.timestamp);
  next_chunk = buff[i].c.id + 1;
}

static void buffer_flush(int id)
{
  int i = id % buff_size;

  while(buff[i].c.data) {
    buffer_free(i);
    i = (i + 1) % buff_size;
    if (i == id % buff_size) {
      break;
    }
  }
}

void tout_init(struct timeval *tcnt, const struct timeval *tconsume);
void consume_chunk(){
  int i=next_chunk % buff_size;
  struct chunk *c = &buff[i].c;
  struct timeval tmp;
  gettimeofday(&tmp, NULL);
  fprintf(stderr,"DEBUG: trying to consume chunk %d, at time %d.%d\n",next_chunk,tmp.tv_sec,tmp.tv_usec);
  if(c->data){
    fprintf(stderr,"DELAYREPORT_Y: delivering chunk %d\n",next_chunk);
    //chunk_write(out, c); //called in buffer_free
    last_chunk = c->id;
    buffer_free(i);
  }else{
    fprintf(stderr,"DELAYREPORT_N: missing chunk %d, can't deliver!\n",next_chunk);
    //this buffer space should be clear, but still...
    free(buff[i].c.data);
    buff[i].c.data = NULL;
    next_chunk++;
  }
}

void output_deliver(const struct chunk *c)
{
  if(fixed_playout_delay != 0){
    if (next_chunk == -1){
      //start chunk consumer
      gettimeofday(&tconsume, NULL);
      tconsume.tv_sec+=fixed_playout_delay;
      fprintf(stderr,"DEBUG: start consuming at time %f, period %f (us)!\n",
              tconsume.tv_sec * 1e6 + tconsume.tv_usec,period.tv_sec * 1e6 + period.tv_usec);
    }
    tout_init(&tcnt, &tconsume);
    //now consume when tcnt=(0,0)
    //TODO: move to separate thread
    while(tcnt.tv_sec <= 0 && tcnt.tv_usec <= 0){
      consume_chunk();
      timeradd(&tconsume,&period,&tconsume);
      tout_init(&tcnt, &tconsume);
    }
  }

  if (!buff) {
    fprintf(stderr, "Warning: code should use output_init!!! Setting output buffer to 8\n");
    output_init(8, NULL);
  }

  if (!reorder) chunk_write(out, c);

  dprintf("Chunk %d delivered\n", c->id);
  buffer_print();
  if (c->id < next_chunk) {
    return;
  }

  /* Initialize buffer with first chunk */
  if (next_chunk == -1) {
    next_chunk = c->id; // FIXME: could be anything between c->id and (c->id - buff_size + 1 > 0) ? c->id - buff_size + 1 : 0
    fprintf(stderr,"First RX Chunk ID: %d\n", c->id);
  }

  if(fixed_playout_delay == 0){
    if (c->id >= next_chunk + buff_size) {
      int i;

      /* We might need some space for storing this chunk,
       * or the stored chunks are too old
       */
      for (i = next_chunk; i <= c->id - buff_size; i++) {
        if (buff[i % buff_size].c.data) {
          buffer_free(i % buff_size);
        } else {
          reg_chunk_playout(c->id, false, c->timestamp); // FIXME: some chunks could be counted as lost at the beginning, depending on the initialization of next_chunk
          next_chunk++;
        }
      }
      buffer_flush(next_chunk);
      dprintf("Next is now %d, chunk is %d\n", next_chunk, c->id);
      fprintf(stderr,"DEBUG: received a chunk too far ahead in the future!\n");
    }
  }

  dprintf("%d == %d?\n", c->id, next_chunk);
  if (fixed_playout_delay == 0 && c->id == next_chunk) {
    dprintf("\tcnt Chunk[%d] - %d: %s\n", c->id, c->id % buff_size, c->data);

    if(start_id == -1 || c->id >= start_id) {
      if(end_id == -1 || c->id <= end_id) {
        if(sflag == 0) {
          fprintf(stderr, "\nFirst chunk id played out: %d\n\n",c->id);
          sflag = 1;
        }
        if (reorder) chunk_write(out, c);
        last_chunk = c->id;
      } else if (eflag == 0 && last_chunk != -1) {
        fprintf(stderr, "\nLast chunk id played out: %d\n\n", last_chunk);
        eflag = 1;
      }
    }
    reg_chunk_playout(c->id, true, c->timestamp);
    next_chunk++;
    buffer_flush(next_chunk);
  } else {
    dprintf("Storing %d (in %d)\n", c->id, c->id % buff_size);
    fprintf(stderr,"DEBUG: Storing %d (in %d)\n", c->id, c->id % buff_size);
    if (buff[c->id % buff_size].c.data) {
      if (buff[c->id % buff_size].c.id == c->id) {
        /* Duplicate of a stored chunk */
        if(chunk_log){fprintf(stderr,"Duplicate! chunkID: %d\n", c->id);}
        dprintf("\tDuplicate!\n");
        reg_chunk_duplicate();
        return;
      }
      fprintf(stderr, "Crap!, chunkid:%d, storedid: %d\n", c->id, buff[c->id % buff_size].c.id);
      exit(-1);
    }
    /* We previously flushed, so we know that c->id is free */
    memcpy(&buff[c->id % buff_size].c, c, sizeof(struct chunk));
    buff[c->id % buff_size].c.data = malloc(c->size);
    memcpy(buff[c->id % buff_size].c.data, c->data, c->size);
  }
}
