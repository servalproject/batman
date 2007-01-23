/*
 * Copyright (C) 2006 B.A.T.M.A.N. contributors:
 * Thomas Lopatic, Corinna 'Elektra' Aichele, Axel Neumann,
 * Felix Fietkau, Marek Lindner
 *
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



#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "os.h"
#include "list.h"
#include "batman.h"
#include "allocate.h"

/* "-d" is the command line switch for the debug level,
 * specify it multiple times to increase verbosity
 * 0 gives a minimum of messages to save CPU-Power
 * 1 normal
 * 2 verbose
 * 3 very verbose
 * Beware that high debugging levels eat a lot of CPU-Power
 */

short debug_level = 0;

/* "-g" is the command line switch for the gateway class,
 * 0 no gateway
 * 1 modem
 * 2 ISDN
 * 3 Double ISDN
 * 3 256 KBit
 * 5 UMTS/ 0.5 MBit
 * 6 1 MBit
 * 7 2 MBit
 * 8 3 MBit
 * 9 5 MBit
 * 10 6 MBit
 * 11 >6 MBit
 * this option is used to determine packet path
 */

char *gw2string[] = { "No Gateway",
                      "56 KBit (e.g. Modem)",
                      "64 KBit (e.g. ISDN)",
                      "128 KBit (e.g. double ISDN)",
                      "256 KBit",
                      "512 KBit (e.g. UMTS)",
                      "1 MBit",
                      "2 MBit",
                      "3 MBit",
                      "5 MBit",
                      "6 MBit",
                      ">6 MBit" };

short gateway_class = 0;

/* "-r" is the command line switch for the routing class,
 * 0 set no default route
 * 1 use fast internet connection
 * 2 use stable internet connection
 * 3 use use best statistic (olsr style)
 * this option is used to set the routing behaviour
 */

short routing_class = 0;


unsigned int orginator_interval = 1000;   /* orginator message interval in miliseconds */

unsigned int bidirectional_timeout = 0;   /* bidirectional neighbour reply timeout in ms */

struct gw_node *curr_gateway = NULL;
pthread_t curr_gateway_thread_id = 0;

unsigned int pref_gateway = 0;

unsigned char *hna_buff = NULL;

short num_hna = 0;
short found_ifs = 0;



static LIST_HEAD(orig_list);
static LIST_HEAD(forw_list);
static LIST_HEAD(gw_list);
LIST_HEAD(if_list);
LIST_HEAD(hna_list);
static unsigned int last_own_packet;

struct vis_if vis_if;
struct unix_if unix_if;
struct debug_clients debug_clients;

void usage(void)
{
	fprintf(stderr, "Usage: batman [options] interface [interface interface]\n" );
	fprintf(stderr, "       -a announce network(s)\n" );
	fprintf(stderr, "       -b run connection in batch mode\n" );
	fprintf(stderr, "       -c connect via unix socket\n" );
	fprintf(stderr, "       -d debug level\n" );
	fprintf(stderr, "       -g gateway class\n" );
	fprintf(stderr, "       -h this help\n" );
	fprintf(stderr, "       -H verbose help\n" );
	fprintf(stderr, "       -o orginator interval in ms\n" );
	fprintf(stderr, "       -p preferred gateway\n" );
	fprintf(stderr, "       -r routing class\n" );
	fprintf(stderr, "       -s visualisation server\n" );
	fprintf(stderr, "       -v print version\n" );
}

void verbose_usage(void)
{
	fprintf(stderr, "Usage: batman [options] interface [interface interface]\n\n" );
	fprintf(stderr, "       -a announce network(s)\n" );
	fprintf(stderr, "          network/netmask is expected\n" );
	fprintf(stderr, "       -b run connection in batch mode\n" );
	fprintf(stderr, "       -c connect to running batmand via unix socket\n" );
	fprintf(stderr, "       -d debug level\n" );
	fprintf(stderr, "          default:         0 -> debug disabled\n" );
	fprintf(stderr, "          allowed values:  1 -> list neighbours\n" );
	fprintf(stderr, "                           2 -> list gateways\n" );
	fprintf(stderr, "                           3 -> observe batman\n" );
	fprintf(stderr, "                           4 -> observe batman (very verbose)\n\n" );
	fprintf(stderr, "       -g gateway class\n" );
	fprintf(stderr, "          default:         0 -> this is not an internet gateway\n" );
	fprintf(stderr, "          allowed values:  1 -> modem line\n" );
	fprintf(stderr, "                           2 -> ISDN line\n" );
	fprintf(stderr, "                           3 -> double ISDN\n" );
	fprintf(stderr, "                           4 -> 256 KBit\n" );
	fprintf(stderr, "                           5 -> UMTS / 0.5 MBit\n" );
	fprintf(stderr, "                           6 -> 1 MBit\n" );
	fprintf(stderr, "                           7 -> 2 MBit\n" );
	fprintf(stderr, "                           8 -> 3 MBit\n" );
	fprintf(stderr, "                           9 -> 5 MBit\n" );
	fprintf(stderr, "                          10 -> 6 MBit\n" );
	fprintf(stderr, "                          11 -> >6 MBit\n\n" );
	fprintf(stderr, "       -h shorter help\n" );
	fprintf(stderr, "       -H this help\n" );
	fprintf(stderr, "       -o orginator interval in ms\n" );
	fprintf(stderr, "          default: 1000, allowed values: >0\n\n" );
	fprintf(stderr, "       -p preferred gateway\n" );
	fprintf(stderr, "          default: none, allowed values: IP\n\n" );
	fprintf(stderr, "       -r routing class (only needed if gateway class = 0)\n" );
	fprintf(stderr, "          default:         0 -> set no default route\n" );
	fprintf(stderr, "          allowed values:  1 -> use fast internet connection\n" );
	fprintf(stderr, "                           2 -> use stable internet connection\n" );
	fprintf(stderr, "                           3 -> use best statistic internet connection (olsr style)\n\n" );
	fprintf(stderr, "       -s visualisation server\n" );
	fprintf(stderr, "          default: none, allowed values: IP\n\n" );
	fprintf(stderr, "       -v print version\n" );

}

/* this function finds or creates an originator entry for the given address if it does not exits */
struct orig_node *get_orig_node( unsigned int addr )
{
	struct list_head *pos;
	struct orig_node *orig_node;

	list_for_each(pos, &orig_list) {
		orig_node = list_entry(pos, struct orig_node, list);
		if (orig_node->orig == addr)
			return orig_node;
	}

	debug_output( 4, "Creating new originator\n" );

	orig_node = debugMalloc( sizeof(struct orig_node), 1 );
	memset(orig_node, 0, sizeof(struct orig_node));
	INIT_LIST_HEAD(&orig_node->list);
	INIT_LIST_HEAD(&orig_node->neigh_list);

	orig_node->orig = addr;
	orig_node->router = NULL;

	orig_node->bidirect_link = debugMalloc( found_ifs * sizeof(int), 2 );
	memset( orig_node->bidirect_link, 0, found_ifs * sizeof(int) );

	list_add_tail( &orig_node->list, &orig_list );

	return orig_node;
}


void add_del_hna( struct orig_node *orig_node, int del ) {

	int hna_buff_count = 0;
	unsigned int hna, netmask;


	while ( ( hna_buff_count + 1 ) * 5 <= orig_node->hna_buff_len ) {

		memcpy( &hna, ( unsigned int *)&orig_node->hna_buff[ hna_buff_count * 5 ], 4 );
		netmask = ( unsigned int )orig_node->hna_buff[ ( hna_buff_count * 5 ) + 4 ];

		if ( ( netmask > 0 ) && ( netmask < 33 ) )
			add_del_route( hna, netmask, orig_node->router->addr, del, orig_node->batman_if->dev, orig_node->batman_if->udp_send_sock );

		hna_buff_count++;

	}

	if ( del ) {

		debugFree( orig_node->hna_buff, 101 );
		orig_node->hna_buff_len = 0;

	}

}



static void choose_gw() {

	struct list_head *pos;
	struct gw_node *gw_node, *tmp_curr_gw = NULL;
	int max_gw_class = 0, max_packets = 0, max_gw_factor = 0;
	static char orig_str[ADDR_STR_LEN];


	if ( routing_class == 0 )
		return;

	if ( list_empty(&gw_list) ) {

		if ( curr_gateway != NULL ) {

			debug_output( 3, "Removing default route - no gateway in range\n" );

			del_default_route();

		}

		return;

	}


	list_for_each(pos, &gw_list) {

		gw_node = list_entry(pos, struct gw_node, list);

		/* ignore this gateway if recent connection attempts were unsuccessful */
		if ( ( gw_node->unavail_factor * gw_node->unavail_factor * 30000 ) + gw_node->last_failure > get_time() )
			continue;

		if ( gw_node->orig_node->router == NULL )
			continue;

		switch ( routing_class ) {

			case 1:   /* fast connection */
				if ( ( ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags ) > max_gw_factor ) || ( ( ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags ) == max_gw_factor ) && ( gw_node->orig_node->router->packet_count > max_packets ) ) )
					tmp_curr_gw = gw_node;
				break;

			case 2:   /* stable connection */
				/* FIXME - not implemented yet */
				if ( ( ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags ) > max_gw_factor ) || ( ( ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags ) == max_gw_factor ) && ( gw_node->orig_node->router->packet_count > max_packets ) ) )
					tmp_curr_gw = gw_node;
				break;

			default:  /* use best statistic (olsr style) */
				if ( gw_node->orig_node->router->packet_count > max_packets )
					tmp_curr_gw = gw_node;
				break;

		}

		if ( gw_node->orig_node->gwflags > max_gw_class )
			max_gw_class = gw_node->orig_node->gwflags;

		if ( gw_node->orig_node->router->packet_count > max_packets )
			max_packets = gw_node->orig_node->router->packet_count;

		if ( ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags ) > max_gw_class )
			max_gw_factor = ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags );

		if ( ( pref_gateway != 0 ) && ( pref_gateway == gw_node->orig_node->orig ) ) {

			tmp_curr_gw = gw_node;

			addr_to_string( tmp_curr_gw->orig_node->orig, orig_str, ADDR_STR_LEN );
			debug_output( 3, "Preferred gateway found: %s (%i,%i,%i)\n", orig_str, gw_node->orig_node->gwflags, gw_node->orig_node->router->packet_count, ( gw_node->orig_node->router->packet_count * gw_node->orig_node->gwflags ) );

			break;

		}

	}


	if ( curr_gateway != tmp_curr_gw ) {

		if ( curr_gateway != NULL ) {

			debug_output( 3, "Removing default route - better gateway found\n" );

			del_default_route();

		}

		curr_gateway = tmp_curr_gw;

		/* may be the last gateway is now gone */
		if ( ( curr_gateway != NULL ) && ( !is_aborted() ) ) {

			addr_to_string( curr_gateway->orig_node->orig, orig_str, ADDR_STR_LEN );
			debug_output( 3, "Adding default route to %s (%i,%i,%i)\n", orig_str, max_gw_class, max_packets, max_gw_factor );

			add_default_route();

		}

	}

}



static void update_routes( struct orig_node *orig_node, struct neigh_node *neigh_node, unsigned char *hna_recv_buff, int hna_buff_len ) {

	static char orig_str[ADDR_STR_LEN], next_str[ADDR_STR_LEN];


	debug_output( 4, "update_routes() \n" );


	if ( ( orig_node != NULL ) && ( orig_node->router != neigh_node ) ) {

		if ( ( orig_node != NULL ) && ( neigh_node != NULL ) ) {
			addr_to_string( orig_node->orig, orig_str, ADDR_STR_LEN );
			addr_to_string( neigh_node->addr, next_str, ADDR_STR_LEN );
			debug_output( 4, "Route to %s via %s\n", orig_str, next_str );
		}

		/* route altered or deleted */
		if ( ( ( orig_node->router != NULL ) && ( neigh_node != NULL ) ) || ( neigh_node == NULL ) ) {

			if ( neigh_node == NULL ) {
				debug_output( 4, "Deleting previous route\n" );
			} else {
				debug_output( 4, "Route changed\n" );
			}

			/* remove old announced network(s) */
			if ( orig_node->hna_buff_len > 0 )
				add_del_hna( orig_node, 1 );

			add_del_route( orig_node->orig, 32, orig_node->router->addr, 1, orig_node->batman_if->dev, orig_node->batman_if->udp_send_sock );

		}

		/* route altered or new route added */
		if ( ( ( orig_node->router != NULL ) && ( neigh_node != NULL ) ) || ( orig_node->router == NULL ) ) {

			if ( orig_node->router == NULL ) {
				debug_output( 4, "Adding new route\n" );
			} else {
				debug_output( 4, "Route changed\n" );
			}

			add_del_route( orig_node->orig, 32, neigh_node->addr, 0, neigh_node->if_incoming->dev, neigh_node->if_incoming->udp_send_sock );

			orig_node->batman_if = neigh_node->if_incoming;
			orig_node->router = neigh_node;

			/* add new announced network(s) */
			if ( hna_buff_len > 0 ) {

				orig_node->hna_buff = debugMalloc( hna_buff_len, 3 );
				orig_node->hna_buff_len = hna_buff_len;

				memcpy( orig_node->hna_buff, hna_recv_buff, hna_buff_len );

				add_del_hna( orig_node, 0 );

			}

		}

		orig_node->router = neigh_node;

	} else if ( orig_node != NULL ) {

		/* may be just HNA changed */
		if ( ( hna_buff_len != orig_node->hna_buff_len ) || ( ( hna_buff_len > 0 ) && ( orig_node->hna_buff_len > 0 ) && ( memcmp(orig_node->hna_buff, hna_recv_buff, hna_buff_len ) != 0 ) ) ) {

			if ( orig_node->hna_buff_len > 0 )
				add_del_hna( orig_node, 1 );

			if ( hna_buff_len > 0 ) {

				orig_node->hna_buff = debugMalloc( hna_buff_len, 4 );
				orig_node->hna_buff_len = hna_buff_len;

				memcpy( orig_node->hna_buff, hna_recv_buff, hna_buff_len );

				add_del_hna( orig_node, 0 );

			}

		}

	}

}

static void update_gw_list( struct orig_node *orig_node, unsigned char new_gwflags ) {

	struct list_head *gw_pos, *gw_pos_tmp;
	struct gw_node *gw_node;
	static char orig_str[ADDR_STR_LEN];

	list_for_each_safe(gw_pos, gw_pos_tmp, &gw_list) {

		gw_node = list_entry(gw_pos, struct gw_node, list);

		if ( gw_node->orig_node == orig_node ) {

			addr_to_string( gw_node->orig_node->orig, orig_str, ADDR_STR_LEN );
			debug_output( 3, "Gateway class of originator %s changed from %i to %i\n", orig_str, gw_node->orig_node->gwflags, new_gwflags );

			if ( new_gwflags == 0 ) {

				list_del( gw_pos );
				debugFree( gw_pos, 102 );

				debug_output( 3, "Gateway %s removed from gateway list\n", orig_str );

			} else {

				gw_node->orig_node->gwflags = new_gwflags;

			}

			choose_gw();
			return;

		}

	}

	addr_to_string( orig_node->orig, orig_str, ADDR_STR_LEN );
	debug_output( 3, "Found new gateway %s -> class: %i - %s\n", orig_str, new_gwflags, gw2string[new_gwflags] );

	gw_node = debugMalloc(sizeof(struct gw_node), 5);
	memset(gw_node, 0, sizeof(struct gw_node));
	INIT_LIST_HEAD(&gw_node->list);

	gw_node->orig_node = orig_node;
	gw_node->unavail_factor = 0;
	gw_node->last_failure = get_time();

	list_add_tail(&gw_node->list, &gw_list);

	choose_gw();

}



void debug() {

	struct list_head *forw_pos, *orig_pos, *neigh_pos;
	struct forw_node *forw_node;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	struct gw_node *gw_node;
	unsigned int batman_count = 0;
	static char str[ADDR_STR_LEN], str2[ADDR_STR_LEN];


	if ( debug_clients.clients_num[1] > 0 ) {

		debug_output( 2, "BOD\n" );

		if ( list_empty(&gw_list) ) {

			debug_output( 2, "No gateways in range ...\n" );

		} else {

			list_for_each(orig_pos, &gw_list) {
				gw_node = list_entry(orig_pos, struct gw_node, list);

				addr_to_string( gw_node->orig_node->orig, str, sizeof (str) );
				addr_to_string( gw_node->orig_node->router->addr, str2, sizeof (str2) );

				if ( curr_gateway == gw_node ) {
					debug_output( 2, "=> %s via: %s(%i), gw_class %i - %s, reliability: %i\n", str, str2, gw_node->orig_node->router->packet_count, gw_node->orig_node->gwflags, gw2string[gw_node->orig_node->gwflags], gw_node->unavail_factor );
				} else {
					debug_output( 2, "%s via: %s(%i), gw_class %i - %s, reliability: %i\n", str, str2, gw_node->orig_node->router->packet_count, gw_node->orig_node->gwflags, gw2string[gw_node->orig_node->gwflags], gw_node->unavail_factor );
				}

			}

		}

	}

	if ( ( debug_clients.clients_num[0] > 0 ) || ( debug_clients.clients_num[3] > 0 ) ) {

		debug_output( 1, "BOD\n" );

		if ( debug_clients.clients_num[3] > 0 ) {

			debug_output( 4, "------------------ DEBUG ------------------\n" );
			debug_output( 4, "Forward list\n" );
 
			list_for_each( forw_pos, &forw_list ) {
				forw_node = list_entry( forw_pos, struct forw_node, list );
				addr_to_string( ((struct packet *)forw_node->pack_buff)->orig, str, sizeof (str) );
				debug_output( 4, "    %s at %u\n", str, forw_node->when );
			}

			debug_output( 4, "Originator list\n" );

		}

		list_for_each(orig_pos, &orig_list) {
			orig_node = list_entry(orig_pos, struct orig_node, list);

			if ( orig_node->router == NULL )
				continue;

			batman_count++;

			addr_to_string( orig_node->orig, str, sizeof (str) );
			addr_to_string( orig_node->router->addr, str2, sizeof (str2) );

			debug_output( 1, "%s, GW: %s(%i) via:", str, str2, orig_node->router->packet_count );
			//printf( "%s, GW: %s(%i) via:", str, str2, orig_node->router->packet_count );
			debug_output( 4, "%s, GW: %s(%i), last_aware:%u via:\n", str, str2, orig_node->router->packet_count, orig_node->last_aware );

			list_for_each(neigh_pos, &orig_node->neigh_list) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				addr_to_string(neigh_node->addr, str, sizeof (str));

				debug_output( 1, " %s(%i)", str, neigh_node->packet_count );
				//printf(" %s(%i)", str, neigh_node->packet_count );
				debug_output( 4, "\t\t%s (%d)\n", str, neigh_node->packet_count );

			}

			debug_output( 1, "\n" );
			//printf( "\n" );

		}

		if ( batman_count == 0 ) {

			debug_output( 1, "No batman nodes in range ...\n" );
			debug_output( 4, "No batman nodes in range ...\n" );

		}

		debug_output( 4, "---------------------------------------------- END DEBUG\n" );

	}

}



int isDuplicate( unsigned int orig, unsigned short seqno ) {

	struct list_head *orig_pos, *neigh_pos;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;

	list_for_each(orig_pos, &orig_list) {
		orig_node = list_entry(orig_pos, struct orig_node, list);

		if ( orig == orig_node->orig ) {

			list_for_each(neigh_pos, &orig_node->neigh_list) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				if ( bit_status( neigh_node->seq_bits, orig_node->last_seqno, seqno ) )
					return 1;

			}

			return 0;

		}

	}

	return 0;

}



int isBidirectionalNeigh( struct orig_node *orig_neigh_node, struct batman_if *if_incoming ) {

	if( orig_neigh_node->bidirect_link[if_incoming->if_num] > 0 && (orig_neigh_node->bidirect_link[if_incoming->if_num] + (bidirectional_timeout)) >= get_time() )
		return 1;
	else
		return 0;

}



void update_originator( struct orig_node *orig_node, struct packet *in, unsigned int neigh, struct batman_if *if_incoming, unsigned char *hna_recv_buff, int hna_buff_len ) {

	struct list_head *neigh_pos;
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node, *best_neigh_node;
	int max_packet_count = 0;
	char is_new_seqno = 0;

	debug_output( 4, "update_originator(): Searching and updating originator entry of received packet,  \n" );


	list_for_each( neigh_pos, &orig_node->neigh_list ) {

		tmp_neigh_node = list_entry(neigh_pos, struct neigh_node, list);

		if ( ( tmp_neigh_node->addr == neigh ) && ( tmp_neigh_node->if_incoming == if_incoming ) ) {

			neigh_node = tmp_neigh_node;

		} else {

			bit_get_packet( tmp_neigh_node->seq_bits, in->seqno - orig_node->last_seqno, 0 );
			tmp_neigh_node->packet_count = bit_packet_count( tmp_neigh_node->seq_bits );

			if ( tmp_neigh_node->packet_count > max_packet_count ) {

				max_packet_count = tmp_neigh_node->packet_count;
				best_neigh_node = tmp_neigh_node;

			}

		}

	}

	if ( neigh_node == NULL ) {

		debug_output( 4, "Creating new last-hop neighbour of originator\n" );

		neigh_node = debugMalloc( sizeof (struct neigh_node), 6 );
		INIT_LIST_HEAD(&neigh_node->list);

		neigh_node->addr = neigh;
		neigh_node->if_incoming = if_incoming;

		list_add_tail(&neigh_node->list, &orig_node->neigh_list);

	} else {

		debug_output( 4, "Updating existing last-hop neighbour of originator\n" );

	}


	is_new_seqno = bit_get_packet( neigh_node->seq_bits, in->seqno - orig_node->last_seqno, 1 );
	neigh_node->packet_count = bit_packet_count( neigh_node->seq_bits );

	if ( neigh_node->packet_count > max_packet_count ) {

		max_packet_count = neigh_node->packet_count;
		best_neigh_node = neigh_node;

	}


	neigh_node->last_ttl = in->ttl; //TBD: This may be applied only if new_seqno is true ??!!
	neigh_node->last_aware = get_time();

	if( is_new_seqno ) {
		debug_output( 4, "updating last_seqno: old %d, new %d \n", orig_node->last_seqno, in->seqno  );
		orig_node->last_seqno = in->seqno;
	}

	/* update routing table and check for changed hna announcements */
	update_routes( orig_node, best_neigh_node, hna_recv_buff, hna_buff_len );

	if ( orig_node->gwflags != in->gwflags )
		update_gw_list( orig_node, in->gwflags );

	orig_node->gwflags = in->gwflags;

}

void schedule_forward_packet( struct packet *in, int unidirectional, int directlink, struct orig_node *orig_node, unsigned int neigh, unsigned char *hna_recv_buff, int hna_buff_len, struct batman_if *if_outgoing ) {

	struct forw_node *forw_node_new;

	debug_output( 4, "schedule_forward_packet():  \n" );

	if ( in->ttl <= 1 ) {

		debug_output( 4, "ttl exceeded \n" );

	} else {

		forw_node_new = debugMalloc( sizeof(struct forw_node), 8 );

		INIT_LIST_HEAD(&forw_node_new->list);

		if ( hna_buff_len > 0 ) {

			forw_node_new->pack_buff = debugMalloc( sizeof(struct packet) + hna_buff_len, 9 );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct packet) );
			memcpy( forw_node_new->pack_buff + sizeof(struct packet), hna_recv_buff, hna_buff_len );
			forw_node_new->pack_buff_len = sizeof(struct packet) + hna_buff_len;

		} else {

			forw_node_new->pack_buff = debugMalloc( sizeof(struct packet), 10 );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct packet) );
			forw_node_new->pack_buff_len = sizeof(struct packet);

		}


		((struct packet *)forw_node_new->pack_buff)->ttl--;
		forw_node_new->when = get_time();
		forw_node_new->own = 0;

		forw_node_new->if_outgoing = if_outgoing;

		/* list_for_each(forw_pos, &forw_list) {
			forw_node = list_entry(forw_pos, struct forw_node, list);
			if ((int)(forw_node->when - forw_node_new->when) > 0)
				break;
		} */

		if ( unidirectional ) {

			((struct packet *)forw_node_new->pack_buff)->flags = ( UNIDIRECTIONAL | DIRECTLINK );

		} else if ( directlink ) {

			((struct packet *)forw_node_new->pack_buff)->flags = DIRECTLINK;

		} else {

			((struct packet *)forw_node_new->pack_buff)->flags = 0x00;

		}

		list_add( &forw_node_new->list, &forw_list );

	}

}

void send_outstanding_packets() {

	struct forw_node *forw_node;
	struct list_head *forw_pos, *if_pos, *temp;
	struct batman_if *batman_if;
	static char orig_str[ADDR_STR_LEN];
	int directlink;

	if ( list_empty( &forw_list ) )
		return;

	list_for_each_safe( forw_pos, temp, &forw_list ) {
		forw_node = list_entry( forw_pos, struct forw_node, list );

		if ( forw_node->when <= get_time() ) {

			addr_to_string( ((struct packet *)forw_node->pack_buff)->orig, orig_str, ADDR_STR_LEN );

			directlink = ( ( ((struct packet *)forw_node->pack_buff)->flags & DIRECTLINK ) ? 1 : 0 );


			if ( ((struct packet *)forw_node->pack_buff)->flags & UNIDIRECTIONAL ) {

				if ( forw_node->if_outgoing != NULL ) {

					debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ((struct packet *)forw_node->pack_buff)->seqno, ((struct packet *)forw_node->pack_buff)->ttl, forw_node->if_outgoing->dev );

					if ( send_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 ) {
						exit( -1 );
					}

				} else {

					debug_output( 0, "Error - can't forward packet with UDF: outgoing iface not specified \n" );

				}

			/* multihomed peer assumed */
			} else if ( ( directlink ) && ( ((struct packet *)forw_node->pack_buff)->ttl == 1 ) ) {

				if ( ( forw_node->if_outgoing != NULL ) ) {

					if ( send_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 ) {
						exit( -1 );
					}

				} else {

					debug_output( 0, "Error - can't forward packet with IDF: outgoing iface not specified (multihomed) \n" );

				}

			} else {

				if ( ( directlink ) && ( forw_node->if_outgoing == NULL ) ) {

					debug_output( 0, "Error - can't forward packet with IDF: outgoing iface not specified \n" );

				} else {

					list_for_each(if_pos, &if_list) {

						batman_if = list_entry(if_pos, struct batman_if, list);

						if ( ( directlink ) && ( forw_node->if_outgoing == batman_if ) ) {
							((struct packet *)forw_node->pack_buff)->flags = DIRECTLINK;
						} else {
							((struct packet *)forw_node->pack_buff)->flags = 0x00;
						}

						debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ((struct packet *)forw_node->pack_buff)->seqno, ((struct packet *)forw_node->pack_buff)->ttl, batman_if->dev );

						/* non-primary interfaces do not send hna information */
						if ( ( forw_node->own ) && ( ((struct packet *)forw_node->pack_buff)->orig != ((struct batman_if *)if_list.next)->addr.sin_addr.s_addr ) ) {

							if ( send_packet( forw_node->pack_buff, sizeof(struct packet), &batman_if->broad, batman_if->udp_send_sock ) < 0 ) {
								exit( -1 );
							}

						} else {

							if ( send_packet( forw_node->pack_buff, forw_node->pack_buff_len, &batman_if->broad, batman_if->udp_send_sock ) < 0 ) {
								exit( -1 );
							}

						}

					}

				}

			}

			list_del( forw_pos );

			debugFree( forw_node->pack_buff, 103 );
			debugFree( forw_node, 104 );

		}

	}

}

void schedule_own_packet() {

	struct forw_node *forw_node_new;
	struct list_head *if_pos;
	struct batman_if *batman_if;
	int curr_time;


	curr_time = get_time();

	if ( ( last_own_packet + orginator_interval ) <= curr_time ) {

		list_for_each(if_pos, &if_list) {

			batman_if = list_entry(if_pos, struct batman_if, list);

			forw_node_new = debugMalloc( sizeof(struct forw_node), 11 );

			INIT_LIST_HEAD(&forw_node_new->list);

			forw_node_new->when = curr_time + rand_num( JITTER );
			forw_node_new->if_outgoing = NULL;
			forw_node_new->own = 1;

			if ( num_hna > 0 ) {

				forw_node_new->pack_buff = debugMalloc( sizeof(struct packet) + num_hna * 5 * sizeof(unsigned char), 12 );
				memcpy( forw_node_new->pack_buff, (unsigned char *)&batman_if->out, sizeof(struct packet) );
				memcpy( forw_node_new->pack_buff + sizeof(struct packet), hna_buff, num_hna * 5 * sizeof(unsigned char) );
				forw_node_new->pack_buff_len = sizeof(struct packet) + num_hna * 5 * sizeof(unsigned char);

			} else {

				forw_node_new->pack_buff = debugMalloc( sizeof(struct packet), 13 );
				memcpy( forw_node_new->pack_buff, &batman_if->out, sizeof(struct packet) );
				forw_node_new->pack_buff_len = sizeof(struct packet);

			}

			list_add( &forw_node_new->list, &forw_list );

			batman_if->out.seqno++;

		}

		last_own_packet = curr_time;

	}

}



void purge( unsigned int curr_time ) {

	struct list_head *orig_pos, *neigh_pos, *orig_temp, *neigh_temp;
	struct list_head *gw_pos, *gw_pos_tmp;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	struct gw_node *gw_node;
	short gw_purged = 0;
	static char orig_str[ADDR_STR_LEN];

	debug_output( 4, "purge() \n" );

	/* for all origins... */
	list_for_each_safe(orig_pos, orig_temp, &orig_list) {
		orig_node = list_entry(orig_pos, struct orig_node, list);

//TBD: (axel) during purge() the orig_node is purged after TIMEOUT while the neigh_nodes later on are purged after 2*TIMEOUT 
		if ( (int)( ( orig_node->last_aware + TIMEOUT ) < curr_time ) ) {

			addr_to_string(orig_node->orig, orig_str, ADDR_STR_LEN);
			debug_output( 4, "Orginator timeout: originator %s, last_aware %u)\n", orig_str, orig_node->last_aware );

			/* for all neighbours towards this orginator ... */
			list_for_each_safe( neigh_pos, neigh_temp, &orig_node->neigh_list ) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				list_del( neigh_pos );
				debugFree( neigh_node, 107 );

			}

			list_for_each_safe(gw_pos, gw_pos_tmp, &gw_list) {

				gw_node = list_entry(gw_pos, struct gw_node, list);

				if ( gw_node->orig_node == orig_node ) {

					addr_to_string( gw_node->orig_node->orig, orig_str, ADDR_STR_LEN );
					debug_output( 3, "Removing gateway %s from gateway list\n", orig_str );

					list_del( gw_pos );
					debugFree( gw_pos, 107 );

					gw_purged = 1;

					break;

				}

			}

			list_del( orig_pos );

			update_routes( orig_node, NULL, NULL, 0 );

			debugFree( orig_node->bidirect_link, 109 );
			debugFree( orig_node, 110 );

		} else {

			/* for all neighbours towards this orginator ... */
			list_for_each_safe( neigh_pos, neigh_temp, &orig_node->neigh_list ) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				if ( (int)( ( neigh_node->last_aware + ( 2 * TIMEOUT ) ) < curr_time ) ) {

					list_del( neigh_pos );
					debugFree( neigh_node, 108 );

				}

			}

		}

	}

	if ( gw_purged )
		choose_gw();

}

void send_vis_packet()
{
	struct list_head *pos;
	struct orig_node *orig_node;
	unsigned char *packet=NULL;

	int step = 5, size=0,cnt=0;

	list_for_each(pos, &orig_list) {
		orig_node = list_entry(pos, struct orig_node, list);
		if ( ( orig_node->router != NULL ) && ( orig_node->orig == orig_node->router->addr ) )
		{
			if(cnt >= size)
			{
				size += step;
				packet = debugRealloc(packet, size * sizeof(unsigned char), 14);
			}
			memmove(&packet[cnt], (unsigned char*)&orig_node->orig,4);
			 *(packet + cnt + 4) = (unsigned char) orig_node->router->packet_count;
			cnt += step;
		}
	}
	if(packet != NULL)
	{
		send_packet(packet, size * sizeof(unsigned char), &vis_if.addr, vis_if.sock);
	 	debugFree( packet, 111 );
	}
}

int batman()
{
	struct list_head *if_pos, *neigh_pos, *hna_pos, *hna_pos_tmp, *forw_pos, *forw_pos_tmp;
	struct orig_node *orig_neigh_node;
	struct batman_if *batman_if, *if_incoming;
	struct neigh_node *neigh_node;
	struct hna_node *hna_node;
	struct forw_node *forw_node;
	unsigned int neigh, hna, netmask, debug_timeout, select_timeout;
	unsigned char in[1501], *hna_recv_buff;
	static char orig_str[ADDR_STR_LEN], neigh_str[ADDR_STR_LEN];
	short forward_old, res, hna_buff_count;
	short if_rp_filter_all_old, if_rp_filter_default_old;
	short is_my_addr, is_my_orig, is_broadcast, is_duplicate, is_bidirectional, forward_duplicate_packet;
	int time_count = 0, curr_time, hna_buff_len;

	last_own_packet = debug_timeout = get_time();
	bidirectional_timeout = orginator_interval * 3;

	if ( !( list_empty( &hna_list ) ) ) {

		list_for_each( hna_pos, &hna_list ) {

			hna_node = list_entry(hna_pos, struct hna_node, list);

			hna_buff = debugRealloc( hna_buff, ( num_hna + 1 ) * 5 * sizeof( unsigned char ), 15 );

			memmove( &hna_buff[ num_hna * 5 ], ( unsigned char *)&hna_node->addr, 4 );
			hna_buff[ ( num_hna * 5 ) + 4 ] = ( unsigned char ) hna_node->netmask;

			num_hna++;

		}

	}

	list_for_each( if_pos, &if_list ) {

		batman_if = list_entry(if_pos, struct batman_if, list);

		batman_if->out.orig = batman_if->addr.sin_addr.s_addr;
		batman_if->out.flags = 0x00;
		batman_if->out.ttl = ( batman_if->if_num == 0 ? TTL : 2 );
		batman_if->out.seqno = 0;
		batman_if->out.gwflags = gateway_class;
		batman_if->out.version = COMPAT_VERSION;
		batman_if->if_rp_filter_old = get_rp_filter( batman_if->dev );
		set_rp_filter( 0 , batman_if->dev );

	}

	if_rp_filter_all_old = get_rp_filter( "all" );
	if_rp_filter_default_old = get_rp_filter( "default" );

	set_rp_filter( 0, "all" );
	set_rp_filter( 0, "default" );

	forward_old = get_forwarding();
	set_forwarding(1);

	while ( !is_aborted() ) {

		debug_output( 4, " \n \n" );

		if(vis_if.sock && time_count == 50)
		{
			time_count = 0;
			send_vis_packet();
		}

		/* harden select_timeout against sudden time change (e.g. ntpdate) */
		curr_time = get_time();
		select_timeout = ( curr_time >= last_own_packet + orginator_interval - 10 ? orginator_interval : last_own_packet + orginator_interval - curr_time );

		res = receive_packet( ( unsigned char *)&in, 1501, &hna_buff_len, &neigh, select_timeout, &if_incoming );

		if ( res < 0 )
			return -1;

		if ( res > 0 ) {

			addr_to_string( ((struct packet *)&in)->orig, orig_str, sizeof(orig_str) );
			addr_to_string( neigh, neigh_str, sizeof(neigh_str) );
			debug_output( 4, "Received BATMAN packet from %s (originator %s, seqno %d, TTL %d)\n", neigh_str, orig_str, ((struct packet *)&in)->seqno, ((struct packet *)&in)->ttl );

			is_my_addr = is_my_orig = is_broadcast = is_duplicate = is_bidirectional = forward_duplicate_packet = 0;

			hna_buff_len -= sizeof(struct packet);
			hna_recv_buff = ( hna_buff_len > 4 ? in + sizeof(struct packet) : NULL );

			list_for_each(if_pos, &if_list) {

				batman_if = list_entry(if_pos, struct batman_if, list);

				if ( neigh == batman_if->addr.sin_addr.s_addr )
					is_my_addr = 1;

				if ( ((struct packet *)&in)->orig == batman_if->addr.sin_addr.s_addr )
					is_my_orig = 1;

				if ( neigh == batman_if->broad.sin_addr.s_addr )
					is_broadcast = 1;

			}


			addr_to_string( ((struct packet *)&in)->orig, orig_str, sizeof (orig_str) );
			addr_to_string( neigh, neigh_str, sizeof (neigh_str) );
			debug_output( 4, "new packet - orig: %s, sender: %s\n", orig_str, neigh_str );

			/*if ( is_duplicate )
				output("Duplicate packet \n");

			if ( in.orig == neigh )
				output("Originator packet from neighbour \n");

			if ( is_my_orig == 1 )
				output("Originator packet from myself (via neighbour) \n");

			if ( in.flags & UNIDIRECTIONAL )
				output("Packet with unidirectional flag \n");

			if ( is_bidirectional )
				output("received via bidirectional link \n");

			if ( !( in.flags & UNIDIRECTIONAL ) && ( !is_bidirectional ) )
				output("neighbour thinks connection is bidirectional - I disagree \n");*/

			if ( ((struct packet *)&in)->gwflags != 0 )
				debug_output( 4, "Is an internet gateway (class %i) \n", ((struct packet *)&in)->gwflags );

			if ( hna_buff_len > 4 ) {

				debug_output( 4, "HNA information received (%i HNA network%s):\n", hna_buff_len / 5, ( hna_buff_len / 5 > 1 ? "s": "" ) );
				hna_buff_count = 0;

				while ( ( hna_buff_count + 1 ) * 5 <= hna_buff_len ) {

					memmove( &hna, ( unsigned int *)&hna_recv_buff[ hna_buff_count * 5 ], 4 );
					netmask = ( unsigned int )hna_recv_buff[ ( hna_buff_count * 5 ) + 4 ];

					addr_to_string( hna, orig_str, sizeof (orig_str) );

					if ( ( netmask > 0 ) && ( netmask < 33 ) )
						debug_output( 4, "hna: %s/%i\n", orig_str, netmask );
					else
						debug_output( 4, "hna: %s/%i -> ignoring (invalid netmask)\n", orig_str, netmask );

					hna_buff_count++;

				}

			}


			if ( ((struct packet *)&in)->version != COMPAT_VERSION ) {

				debug_output( 4, "Drop packet: incompatible batman version (%i) \n", ((struct packet *)&in)->version );

			} else if ( is_my_addr ) {

				debug_output( 4, "Drop packet: received my own broadcast (sender: %s)\n", neigh_str );

			} else if ( is_broadcast ) {

				debug_output( 4, "Drop packet: ignoring all packets with broadcast source IP (sender: %s)\n", neigh_str );

			} else if ( is_my_orig ) {

				orig_neigh_node = get_orig_node( neigh );

				orig_neigh_node->last_aware = get_time();

				/* neighbour has to indicate direct link and it has to come via the corresponding interface */
				if ( ( ((struct packet *)&in)->flags & DIRECTLINK ) && ( if_incoming->addr.sin_addr.s_addr == ((struct packet *)&in)->orig ) ) {

					orig_neigh_node->bidirect_link[if_incoming->if_num] = get_time();

					debug_output( 4, "received my own packet from neighbour indicating bidirectional link, updating bidirect_link timestamp \n");

				}

				debug_output( 4, "Drop packet: originator packet from myself (via neighbour) \n" );

			} else if ( ((struct packet *)&in)->flags & UNIDIRECTIONAL ) {

				debug_output( 4, "Drop packet: originator packet with unidirectional flag \n" );

			} else {

				orig_neigh_node = get_orig_node( neigh );

				orig_neigh_node->last_aware = get_time();

				is_duplicate = isDuplicate( ((struct packet *)&in)->orig, ((struct packet *)&in)->seqno );
				is_bidirectional = isBidirectionalNeigh( orig_neigh_node, if_incoming );

				/* update ranking */
				if ( ( is_bidirectional ) && ( !is_duplicate ) ) {
//TBD: (axel) here the real originator should be updated but the orig_neigh_node ist provided to update_originator()
					update_originator( orig_neigh_node, (struct packet *)&in, neigh, if_incoming, hna_recv_buff, hna_buff_len );
				}
	
				/* is single hop (direct) neighbour */
				if ( ((struct packet *)&in)->orig == neigh ) {

					/* it is our best route towards him */
					if ( ( is_bidirectional ) && ( orig_neigh_node->router != NULL ) && ( orig_neigh_node->router->addr == neigh ) ) {

						/* mark direct link on incoming interface */
						schedule_forward_packet( (struct packet *)&in, 0, 1, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

						debug_output( 4, "Forward packet: rebroadcast neighbour packet with direct link flag \n" );

					/* if an unidirectional neighbour sends us a packet - retransmit it with unidirectional flag to tell him that we get its packets */
					/* if a bidirectional neighbour sends us a packet - retransmit it with unidirectional flag if it is not our best link to it in order to prevent routing problems */
					} else if ( ( ( is_bidirectional ) && ( ( orig_neigh_node->router == NULL ) || ( orig_neigh_node->router->addr != neigh ) ) ) || ( !is_bidirectional ) ) {

						schedule_forward_packet( (struct packet *)&in, 1, 1, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

						debug_output( 4, "Forward packet: rebroadcast neighbour packet with direct link and unidirectional flag \n" );

					}

				/* multihop orginator */
				} else {

					if ( is_bidirectional ) {

						if ( !is_duplicate ) {

							schedule_forward_packet( (struct packet *)&in, 0, 0, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

							debug_output( 4, "Forward packet: rebroadcast orginator packet \n" );

						} else if ( orig_neigh_node->router->addr == neigh ) {

							list_for_each(neigh_pos, &orig_neigh_node->neigh_list) {

								neigh_node = list_entry(neigh_pos, struct neigh_node, list);

								if ( ( neigh_node->addr == neigh ) && ( neigh_node->if_incoming == if_incoming ) ) {

									if ( neigh_node->last_ttl == ((struct packet *)&in)->ttl )
										forward_duplicate_packet = 1;

									break;

								}

							}

							/* we are forwarding duplicate o-packets if they come via our best neighbour and ttl is valid */
							if ( forward_duplicate_packet ) {

								schedule_forward_packet( (struct packet *)&in, 0, 0, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

								debug_output( 4, "Forward packet: duplicate packet received via best neighbour with best ttl \n" );

							} else {

								debug_output( 4, "Drop packet: duplicate packet received via best neighbour but not best ttl \n" );

							}

						} else {

							debug_output( 4, "Drop packet: duplicate packet (not received via best neighbour) \n" );

						}

					} else {

						debug_output( 4, "Drop packet: received via unidirectional link \n" );

					}

				}

			}

		}

		schedule_own_packet();

		send_outstanding_packets();

		if ( ( routing_class != 0 ) && ( curr_gateway == NULL ) )
			choose_gw();

		purge( get_time() );

		if ( debug_timeout + 1000 < get_time() ) {

			debug_timeout = get_time();

			debug();

			checkIntegrity();

		}

		time_count++;

	}

	if ( debug_level > 0 )
		printf( "Deleting all BATMAN routes\n" );

	purge( get_time() + TIMEOUT + orginator_interval );


	list_for_each_safe( hna_pos, hna_pos_tmp, &hna_list ) {

		hna_node = list_entry(hna_pos, struct hna_node, list);

		debugFree( hna_node, 114 );

	}

	if ( hna_buff != NULL )
		debugFree( hna_buff, 115 );


	list_for_each_safe( forw_pos, forw_pos_tmp, &forw_list ) {
		forw_node = list_entry( forw_pos, struct forw_node, list );

		list_del( forw_pos );

		debugFree( forw_node->pack_buff, 112 );
		debugFree( forw_node, 113 );

	}

	set_forwarding( forward_old );

	list_for_each(if_pos, &if_list) {
		batman_if = list_entry(if_pos, struct batman_if, list);
		set_rp_filter( batman_if->if_rp_filter_old , batman_if->dev );
	}

	set_rp_filter( if_rp_filter_all_old, "all" );
	set_rp_filter( if_rp_filter_default_old, "default" );


	return 0;

}
