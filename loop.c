/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#ifndef _WIN32
#include <sys/select.h>
#else
#include <winsock2.h>
#endif
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <net_helper.h>
#include <grapes_msg_types.h>
#include <peerset.h>
#include <peer.h>

#include "compatibility/timer.h"

#include "chunk_signaling.h"
#include "streaming.h"
#include "topology.h"
#include "loop.h"
#include "dbg.h"

#define BUFFSIZE 512 * 1024
#define FDSSIZE 16
struct timeval period = {0, 500000};

//calculate timeout based on tnext
void tout_init(struct timeval *tout, const struct timeval *tnext)
{
  struct timeval tnow;

  gettimeofday(&tnow, NULL);
  if(timercmp(&tnow, tnext, <)) {
    timersub(tnext, &tnow, tout);
  } else {
    *tout = (struct timeval){0, 0};
  }
}

void loop(struct nodeID *s, int csize, int buff_size)
{
  int done = 0;
  static uint8_t buff[BUFFSIZE];
  int cnt = 0;
  struct timeval tnext;
  
  gettimeofday(&tnext, NULL);
  period.tv_sec = csize / 1000000;
  period.tv_usec = csize % 1000000;
  
  peers_init();
  stream_init(buff_size, s);
  update_peers(NULL, NULL, 0);
  while (!done) {
    int len, res;
    struct timeval tv;

    tout_init(&tv, &tnext);
    res = wait4data(s, &tv, NULL);
    if (res > 0) {
      struct nodeID *remote;

      len = recv_from_peer(s, &remote, buff, BUFFSIZE);
      if (len < 0) {
        fprintf(stderr,"Error receiving message. Maybe larger than %d bytes\n", BUFFSIZE);
        nodeid_free(remote);
        continue;
      }
      switch (buff[0] /* Message Type */) {
        case MSG_TYPE_TMAN:
        case MSG_TYPE_STREAMER_TOPOLOGY:
        case MSG_TYPE_TOPOLOGY:
          dtprintf("Topo message received:\n");
          update_peers(remote, buff, len);
          break;
        case MSG_TYPE_CHUNK:
          dtprintf("Chunk message received:\n");
          received_chunk(remote, buff, len);
          break;
        case MSG_TYPE_SIGNALLING:
          dtprintf("Sign message received:\n");
          sigParseData(remote, buff, len);
          break;
        default:
          fprintf(stderr, "Unknown Message Type %x\n", buff[0]);
      }
      nodeid_free(remote);
    } else {
      struct timeval tmp;
      //send_offer();
      send_chunk_request();
      if (cnt++ % 10 == 0) {
        update_peers(NULL, NULL, 0);
      }
      timeradd(&tnext, &period, &tmp);
      tnext = tmp;
    }
  }
}

void source_loop(const char *fname, struct nodeID *s, int csize, int chunks, int buff_size)
{
  int done = 0;
  static uint8_t buff[BUFFSIZE];
  int cnt = 0;
  struct timeval tnext;
  struct timeval tnextp;	//next offer time instance
  int fds[FDSSIZE];
  fds[0] = -1;

  gettimeofday(&tnext, NULL);
  gettimeofday(&tnextp, NULL);
  period.tv_sec = csize  / 1000000;
  period.tv_usec = csize % 1000000;
  
  peers_init();

  if (source_init(fname, s, fds, FDSSIZE, buff_size) < 0) {
    fprintf(stderr,"Cannot initialize source, exiting");
    return;
  }
  while (!done) {
    int len, res;
    struct timeval tv, *ptv;
    int wait4fds[FDSSIZE], *pfds;

    if (fds[0] == -1) {
      tout_init(&tv, &tnext);
      ptv = &tv;
      pfds = NULL;
    } else {
      memcpy(wait4fds, fds, sizeof(fds));
      pfds = wait4fds;
      tout_init(&tv, &tnext);
      ptv = &tv;
    }
    res = wait4data(s, ptv, pfds);

    if (res == 1) {
      struct nodeID *remote;

      len = recv_from_peer(s, &remote, buff, BUFFSIZE);
      if (len < 0) {
        fprintf(stderr,"Error receiving message. Maybe larger than %d bytes\n", BUFFSIZE);
        nodeid_free(remote);
        continue;
      }
      dprintf("Received message (%d) from %s\n", buff[0], node_addr(remote));
      switch (buff[0] /* Message Type */) {
	case MSG_TYPE_TMAN:
        case MSG_TYPE_STREAMER_TOPOLOGY:
        case MSG_TYPE_TOPOLOGY:
          fprintf(stderr, "Top Parse\n");
          update_peers(remote, buff, len);
          break;
        case MSG_TYPE_CHUNK:
          fprintf(stderr, "Some dumb peer pushed a chunk to me! peer:%s\n",node_addr(remote));
          break;
        case MSG_TYPE_SIGNALLING:
          sigParseData(remote, buff, len);
          break;
        default:
          fprintf(stderr, "Bad Message Type %x\n", buff[0]);
      }
      nodeid_free(remote);
    } else if (res == 0 || res == 2) {	//timeout or data arrived from source
      int i;
      struct timeval tmp, d;
      struct timeval tnow;
      struct chunk *c;

      c = generated_chunk(&d.tv_usec);
      d.tv_sec = d.tv_usec / 1000000;
      d.tv_usec %= 1000000;
      if (c) {
        add_chunk(c);
        for (i = 0; i < chunks; i++) {	// @TODO: why this cycle?
          send_chunk();
        }
        if (cnt++ % 10 == 0) {
            update_peers(NULL, NULL, 0);
        }
      }

      //check whether to send offer
      gettimeofday(&tnow, NULL);
      if(timercmp(&tnow, &tnextp, >=)) {
        send_offer();
        if (cnt++ % 10 == 0) {
          update_peers(NULL, NULL, 0);
        }
        timeradd(&tnextp, &period, &tmp);
        tnextp = tmp;
      }

      //calculate next timeout
      timeradd(&tnow, &d, &tnext);
      if(timercmp(&tnextp, &tnext, <)) {
        tnext = tnextp;
      }
    }
  }
}
