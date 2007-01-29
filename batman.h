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
#include <sys/un.h>
#include <stdint.h>
#include "list.h"


#define SOURCE_VERSION "0.2 early alpha"
#define COMPAT_VERSION 2
#define PORT 1966
#define UNIDIRECTIONAL 0x80
#define DIRECTLINK 0x40
#define ADDR_STR_LEN 16

#define UNIX_PATH "/var/run/batmand.socket"


/*
 * No configuration files or fancy command line switches yet
 * To experiment with B.A.T.M.A.N. settings change them here
 * and recompile the code
 * Here is the stuff you may want to play with:
 */

#define JITTER 50
#define TTL 50             /* Time To Live of broadcast messages */
#define TIMEOUT 60000      /* sliding window size of received orginator messages in ms */
#define SEQ_RANGE 60       /* sliding packet range of received orginator messages in squence numbers (should be a multiple of our word size) */



#define TYPE_OF_WORD unsigned long /* you should choose something big, if you don't want to waste cpu */

#define MIN_NUM_WORDS ( SEQ_RANGE / ( sizeof(TYPE_OF_WORD) * 8 ) )
#define RST_NUM_WORDS ( SEQ_RANGE % ( sizeof(TYPE_OF_WORD) * 8 ) )
#define NUM_WORDS ( RST_NUM_WORDS ? ( MIN_NUM_WORDS + 1) : MIN_NUM_WORDS )
#define WORD_BIT_SIZE ( sizeof(TYPE_OF_WORD) * 8 )




extern short debug_level;
extern short gateway_class;
extern short routing_class;
extern short num_hna;
extern unsigned long orginator_interval;
extern unsigned int pref_gateway;

extern unsigned char *hna_buff;

extern struct gw_node *curr_gateway;
pthread_t curr_gateway_thread_id;

extern short found_ifs;
extern int receive_max_sock;
extern fd_set receive_wait_set;

extern struct list_head if_list;
extern struct list_head hna_list;
extern struct vis_if vis_if;
extern struct unix_if unix_if;
extern struct debug_clients debug_clients;

struct packet
{
	uint32_t  	orig;
	uint8_t  	flags;    /* 0xF0: UNIDIRECTIONAL link, 0x80: DIRECTLINK flag, ... */
	uint8_t  	ttl;
	uint16_t	seqno;
	uint8_t	  	gwflags;  /* flags related to gateway functions: gateway class */
	uint8_t	  	version;  /* batman version field */
} __attribute__((packed));

struct orig_node                 /* structure for orig_list maintaining nodes of mesh */
{
	struct list_head list;
	unsigned int orig;
	struct neigh_node *router;
	struct batman_if *batman_if;
	unsigned int *bidirect_link;    /* if node is a bidrectional neighbour, when my originator packet was broadcasted (replied) by this node and received by me */
	unsigned int last_aware;        /* when last packet from this node was received */
	unsigned char gwflags;          /* flags related to gateway functions: gateway class */
	unsigned char *hna_buff;
	int hna_buff_len;
	uint16_t last_seqno;      /* last known squence number */
	struct list_head neigh_list;
};

struct neigh_node
{
	struct list_head list;
	unsigned int addr;
	unsigned short packet_count;
	unsigned char last_ttl;         /* ttl of last received packet */
	unsigned int last_aware;        /* when last packet via this neighbour was received */
	TYPE_OF_WORD seq_bits[ NUM_WORDS ];
	struct batman_if *if_incoming;
};

struct hna_node
{
	struct list_head list;
	unsigned int addr;
	unsigned int netmask;
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
	short if_num;
	short if_rp_filter_old;
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

struct unix_if {
	int unix_sock;
	pthread_t listen_thread_id;
	struct sockaddr_un addr;
	struct list_head client_list;
};

struct unix_client {
	struct list_head list;
	int sock;
	char debug_level;
	struct sockaddr_un addr;
};

struct debug_clients {
	void *fd_list[4];
	short clients_num[4];
};

struct debug_level_info {
	struct list_head list;
	int fd;
};


int batman();
void usage(void);
void verbose_usage(void);
void del_default_route();
int add_default_route();

void bit_init( TYPE_OF_WORD *seq_bits );
int  get_bit_status( TYPE_OF_WORD *seq_bits, uint16_t last_seqno, uint16_t curr_seqno );
char* bit_print( TYPE_OF_WORD *seq_bits );
void bit_mark( TYPE_OF_WORD *seq_bits, int n );
void bit_shift( TYPE_OF_WORD *seq_bits, int n );
char bit_get_packet( TYPE_OF_WORD *seq_bits, int seq_num_diff, int set_mark );
int  bit_packet_count( TYPE_OF_WORD *seq_bits );

#endif
