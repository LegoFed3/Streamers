/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <msg_types.h>
#include <net_helper.h>
#include <topmanager.h>

#include "net_helpers.h"
#include "loop.h"
#include "output.h"

static const char *my_iface = NULL;
static int port = 6666;
static int srv_port;
static const char *srv_ip = "";
static int period = 40;
static int chunks_per_second = 25;
static int multiply = 1;
static int buff_size = 50;
static int outbuff_size = 25;
static const char *fname = "input.mpg";
static bool loop_input = false;
unsigned char msgTypes[] = {MSG_TYPE_TOPOLOGY,MSG_TYPE_CHUNK,MSG_TYPE_SIGNALLING};

static void cmdline_parse(int argc, char *argv[])
{
  int o;

  while ((o = getopt(argc, argv, "b:o:c:t:p:i:P:I:f:m:l")) != -1) {
    switch(o) {
      case 'b':
        buff_size = atoi(optarg);
        break;
      case 'o':
        outbuff_size = atoi(optarg);
        break;
      case 'c':
        chunks_per_second = atoi(optarg);
        break;
      case 'm':
        multiply = atoi(optarg);
        break;
      case 't':
        period = atoi(optarg);
        break;
      case 'p':
        srv_port = atoi(optarg);
        break;
      case 'i':
        srv_ip = strdup(optarg);
        break;
      case 'P':
        port =  atoi(optarg);
        break;
      case 'I':
        my_iface = strdup(optarg);
        break;
      case 'f':
        fname = strdup(optarg);
        break;
      case 'l':
        loop_input = true;
        break;
      default:
        fprintf(stderr, "Error: unknown option %c\n", o);

        exit(-1);
    }
  }
}

static struct nodeID *init(void)
{
  int i;
  struct nodeID *myID;
  char *my_addr;

  if (my_iface) {
    my_addr = iface_addr(my_iface);
  } else {
    my_addr = default_ip_addr(my_iface);
  }

  if (my_addr == NULL) {
    fprintf(stderr, "Cannot find network interface %s\n", my_iface);

    return NULL;
  }
  for (i=0;i<3;i++)
	  bind_msg_type(msgTypes[i]);
  myID = net_helper_init(my_addr, port);
  if (myID == NULL) {
    fprintf(stderr, "Error creating my socket (%s:%d)!\n", my_addr, port);
    free(my_addr);

    return NULL;
  }
  free(my_addr);
  topInit(myID);

  output_init(outbuff_size);

  return myID;
}


int main(int argc, char *argv[])
{
  struct nodeID *my_sock;

  cmdline_parse(argc, argv);

  my_sock = init();
  if (my_sock == NULL) {
    return -1;
  }
  if (srv_port != 0) {
    struct nodeID *srv;

    srv = create_node(srv_ip, srv_port);
    if (srv == NULL) {
      fprintf(stderr, "Cannot resolve remote address %s:%d\n", srv_ip, srv_port);

      return -1;
    }
    topAddNeighbour(srv);

    loop(my_sock, 1000000 / chunks_per_second, buff_size);
  }

  source_loop(fname, my_sock, period * 1000, multiply, loop_input);

  return 0;
}