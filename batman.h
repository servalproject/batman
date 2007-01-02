/*
 * Copyright (C) 2006 B.A.T.M.A.N. contributors:
 * Thomas Lopatic, Corinna 'Elektra' Aichele, Axel Neumann, Marek Lindner
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _BATMAN_BATMAN_H
#define _BATMAN_BATMAN_H

#include <netinet/in.h>
#include <pthread.h>
#include "list.h"

#define SOURCE_VERSION "0.2 early alpha"
#define COMPAT_VERSION 2
#define PORT 1966
#define UNIDIRECTIONAL 0x80
#define DIRECTLINK 0x40
#define ADDR_STR_LEN 16

/*
 * No configuration files or fancy command line switches yet
 * To experiment with B.A.T.M.A.N. settings change them here
 * and recompile the code
 * Here is the stuff you may want to play with:
 */

#define TTL 50            /* Time To Live of broadcast messages */
#define TIMEOUT 60000     /* sliding window size of received orginator messages in ms */
#define JITTER 50



extern int debug_level;
extern int orginator_interval;
extern int gateway_class;
extern int routing_class;
extern int num_hna;
extern unsigned int pref_gateway;

extern unsigned char *hna_buff;

extern struct gw_node *curr_gateway;
pthread_t curr_gateway_thread_id;

extern int found_ifs;

extern struct list_head if_list;
extern struct list_head hna_list;
extern struct vis_if vis_if;

struct packet
{
	unsigned long  orig;
	unsigned char  flags;    /* 0xF0: UNIDIRECTIONAL link, 0x80: DIRECTLINK flag, ... */
	unsigned char  ttl;
	unsigned short seqno;
	unsigned char  gwflags;  /* flags related to gateway functions: gateway class */
	unsigned char  version;  /* batman version field */
} __attribute__((packed));

struct orig_node                 /* structure for orig_list maintaining nodes of mesh */
{
	struct list_head list;
	unsigned int orig;
	unsigned int router;
	struct batman_if *batman_if;
	unsigned int packet_count;  /* packets gathered from its router */
	unsigned int last_seen;     /* when last originator packet (with new seq-number) from this node was received */
	unsigned int *last_reply;   /* if node is a neighbour, when my originator packet was last broadcasted (replied) by this node and received by me */
	unsigned int last_aware;    /* if node is a neighbour, when last packet via this node was received */
	unsigned char flags;
	unsigned char gwflags;      /* flags related to gateway functions: gateway class */
	unsigned char *hna_buff;
	int hna_buff_len;
	struct list_head neigh_list;
	struct list_head hna_list;
};

struct neigh_node
{
	struct list_head list;
	unsigned int addr;
	unsigned int packet_count; /* packets gathered from this neighbour */
	unsigned short best_ttl;   /* ttl of last packet */
	struct list_head pack_list;
};

struct hna_node
{
	struct list_head list;
	unsigned int addr;
	unsigned int netmask;
};


struct pack_node
{
	struct list_head list;
	unsigned int time;
	unsigned short seqno;
	unsigned char ttl;
	struct batman_if *if_incoming;
};

struct forw_node                 /* structure for forw_list maintaining packets to be send/forwarded */
{
	struct list_head list;
	unsigned int when;
	int own;
	unsigned char *pack_buff;
	int pack_buff_len;
	struct batman_if *if_outgoing;
};

struct gw_node
{
	struct list_head list;
	struct orig_node *orig_node;
	int unavail_factor;
	int last_failure;
};

struct batman_if
{
	struct list_head list;
	char *dev;
	int udp_send_sock;
	int udp_recv_sock;
	int tcp_gw_sock;
	int tunnel_sock;
	int if_num;
	int if_rp_filter_old;
	pthread_t listen_thread_id;
	struct sockaddr_in addr;
	struct sockaddr_in broad;
	struct packet out;
	struct list_head client_list;
};

struct gw_client
{
	struct list_head list;
	struct batman_if *batman_if;
	int sock;
	unsigned int last_keep_alive;
	struct sockaddr_in addr;
};

struct vis_if {
	int sock;
	struct sockaddr_in addr;
};



int batman();
void usage(void);
void verbose_usage(void);
void del_default_route();
int add_default_route();

#endif
