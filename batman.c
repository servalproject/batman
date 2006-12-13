/*
 * Copyright (C) 2006 BATMAN contributors:
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

/* "-d" is the command line switch for the debug level,
 * specify it multiple times to increase verbosity
 * 0 gives a minimum of messages to save CPU-Power
 * 1 normal
 * 2 verbose
 * 3 very verbose
 * Beware that high debugging levels eat a lot of CPU-Power
 */

int debug_level = 0;

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

int gateway_class = 0;

/* "-r" is the command line switch for the routing class,
 * 0 set no default route
 * 1 use fast internet connection
 * 2 use stable internet connection
 * 3 use use best statistic (olsr style)
 * this option is used to set the routing behaviour
 */

int routing_class = 0;


int orginator_interval = 1000;   /* orginator message interval in miliseconds */

int bidirectional_timeout = 0;   /* bidirectional neighbour reply timeout in ms */

struct gw_node *curr_gateway = NULL;
pthread_t curr_gateway_thread_id = 0;

unsigned int pref_gateway = 0;

unsigned char *hna_buff = NULL;

int num_hna = 0;
int found_ifs = 0;



static LIST_HEAD(orig_list);
static LIST_HEAD(forw_list);
static LIST_HEAD(gw_list);
LIST_HEAD(if_list);
LIST_HEAD(hna_list);
static unsigned int last_own_packet;

struct vis_if vis_if;

void usage(void)
{
	fprintf(stderr, "Usage: batman [options] interface [interface interface]\n" );
	fprintf(stderr, "       -a announce network(s)\n" );
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

	if (debug_level == 4)
		output("Creating new originator\n");

	orig_node = alloc_memory(sizeof(struct orig_node));
	memset(orig_node, 0, sizeof(struct orig_node));
	INIT_LIST_HEAD(&orig_node->list);
	INIT_LIST_HEAD(&orig_node->neigh_list);

	orig_node->orig = addr;
	orig_node->gwflags = 0;
	orig_node->packet_count = 0;
	orig_node->hna_buff_len = 0;

	orig_node->last_reply = alloc_memory( found_ifs * sizeof(int) );
	memset( orig_node->last_reply, 0, found_ifs * sizeof(int) );

	list_add_tail(&orig_node->list, &orig_list);

	return orig_node;
}


void add_del_hna( struct orig_node *orig_node, int del ) {

	int hna_buff_count = 0;
	unsigned int hna, netmask;


	while ( ( hna_buff_count + 1 ) * 5 <= orig_node->hna_buff_len ) {

		memcpy( &hna, ( unsigned int *)&orig_node->hna_buff[ hna_buff_count * 5 ], 4 );
		netmask = ( unsigned int )orig_node->hna_buff[ ( hna_buff_count * 5 ) + 4 ];

		if ( ( netmask > 0 ) && ( netmask < 33 ) )
			add_del_route( hna, netmask, orig_node->router, del, orig_node->batman_if->dev, orig_node->batman_if->udp_send_sock );

		hna_buff_count++;

	}

	if ( del ) {

		free_memory( orig_node->hna_buff );
		orig_node->hna_buff_len = 0;

	}

}



static void choose_gw()
{
	struct list_head *pos;
	struct gw_node *gw_node, *tmp_curr_gw = NULL;
	int max_gw_class = 0, max_packets = 0, max_gw_factor = 0;
	static char orig_str[ADDR_STR_LEN];


	if ( routing_class == 0 )
		return;

	if ( list_empty(&gw_list) ) {

		if ( curr_gateway != NULL ) {

			if (debug_level == 3)
				printf( "Removing default route - no gateway in range\n" );

			del_default_route();

		}

		return;

	}


	list_for_each(pos, &gw_list) {

		gw_node = list_entry(pos, struct gw_node, list);

		/* ignore this gateway if recent connection attempts were unsuccessful */
		if ( ( gw_node->unavail_factor * gw_node->unavail_factor * 30000 ) + gw_node->last_failure > get_time() )
			continue;

		switch ( routing_class ) {

			case 1:   /* fast connection */
				if ( ( ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags ) > max_gw_factor ) || ( ( ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags ) == max_gw_factor ) && ( gw_node->orig_node->packet_count > max_packets ) ) )
					tmp_curr_gw = gw_node;
				break;

			case 2:   /* stable connection */
				/* FIXME - not implemented yet */
				if ( ( ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags ) > max_gw_factor ) || ( ( ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags ) == max_gw_factor ) && ( gw_node->orig_node->packet_count > max_packets ) ) )
					tmp_curr_gw = gw_node;
				break;

			default:  /* use best statistic (olsr style) */
				if ( gw_node->orig_node->packet_count > max_packets )
					tmp_curr_gw = gw_node;
				break;

		}

		if ( gw_node->orig_node->gwflags > max_gw_class )
			max_gw_class = gw_node->orig_node->gwflags;

		if ( gw_node->orig_node->packet_count > max_packets )
			max_packets = gw_node->orig_node->packet_count;

		if ( ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags ) > max_gw_class )
			max_gw_factor = ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags );

		if ( ( pref_gateway != 0 ) && ( pref_gateway == gw_node->orig_node->orig ) ) {

			tmp_curr_gw = gw_node;

			if (debug_level == 3) {
				addr_to_string( tmp_curr_gw->orig_node->orig, orig_str, ADDR_STR_LEN );
				printf( "Preferred gateway found: %s (%i,%i,%i)\n", orig_str, gw_node->orig_node->gwflags, gw_node->orig_node->packet_count, ( gw_node->orig_node->packet_count * gw_node->orig_node->gwflags ) );
			}

			break;

		}

	}


	if ( curr_gateway != tmp_curr_gw ) {

		if ( curr_gateway != NULL ) {

			if (debug_level == 3)
				printf( "Removing default route - better gateway found\n" );

			del_default_route();

		}

		curr_gateway = tmp_curr_gw;

		/* may be the last gateway is now gone */
		if ( ( curr_gateway != NULL ) && ( !is_aborted() ) ) {

			if (debug_level == 3) {
				addr_to_string( curr_gateway->orig_node->orig, orig_str, ADDR_STR_LEN );
				printf( "Adding default route to %s (%i,%i,%i)\n", orig_str, max_gw_class, max_packets, max_gw_factor );
			}

			add_default_route();

		}

	}

}



static void update_routes( struct orig_node *orig_node, unsigned char *hna_recv_buff, int hna_buff_len )
{

	struct list_head *neigh_pos, *pack_pos;
	struct neigh_node *neigh_node, *next_hop;
	struct pack_node *pack_node;
	struct batman_if *max_if;
	int max_pack, max_ttl, neigh_ttl[found_ifs], neigh_pkts[found_ifs];
	static char orig_str[ADDR_STR_LEN], next_str[ADDR_STR_LEN];

	if ( debug_level == 4 )
		output( "update_routes() \n" );

	max_ttl  = 0;
	max_pack = 0;
	next_hop = NULL;

	/* for every neighbour... */
	list_for_each( neigh_pos, &orig_node->neigh_list ) {
		neigh_node = list_entry( neigh_pos, struct neigh_node, list );

		memset( neigh_pkts, 0, sizeof(neigh_pkts) );
		memset( neigh_ttl, 0, sizeof(neigh_ttl) );

		max_if = (struct batman_if *)if_list.next; /* first batman interface */

		list_for_each( pack_pos, &neigh_node->pack_list ) {
			pack_node = list_entry( pack_pos, struct pack_node, list );
			if ( pack_node->ttl > neigh_ttl[pack_node->if_incoming->if_num] )
				neigh_ttl[pack_node->if_incoming->if_num] = pack_node->ttl;

			neigh_pkts[pack_node->if_incoming->if_num]++;
			if ( neigh_pkts[pack_node->if_incoming->if_num] > neigh_pkts[max_if->if_num] )
				max_if = pack_node->if_incoming;
		}

		neigh_node->packet_count = neigh_pkts[max_if->if_num];

		/* if received most orig_packets via this neighbour (or better ttl) then
			select this neighbour as next hop for this origin */
		if ( ( neigh_pkts[max_if->if_num] > max_pack ) || ( ( neigh_pkts[max_if->if_num] == max_pack ) && ( neigh_ttl[max_if->if_num] > max_ttl ) ) ) {

			max_pack = neigh_pkts[max_if->if_num];
			max_ttl = neigh_ttl[max_if->if_num];

			next_hop = neigh_node;
			if ( debug_level == 4 )
				output( "%d living received packets via selected router \n", neigh_pkts[max_if->if_num] );

		}

	}

	if ( next_hop != NULL ) {

		if ( debug_level == 4 ) {
			addr_to_string( orig_node->orig, orig_str, ADDR_STR_LEN );
			addr_to_string( next_hop->addr, next_str, ADDR_STR_LEN );
			output( "Route to %s via %s\n", orig_str, next_str );
		}

		orig_node->packet_count = neigh_pkts[max_if->if_num];

		if ( orig_node->router != next_hop->addr ) {

			if ( debug_level == 4 )
				output( "Route changed\n" );

			if ( orig_node->router != 0 ) {

				if ( debug_level == 4 )
					output( "Deleting previous route\n" );

				/* remove old announced network(s) */
				if ( orig_node->hna_buff_len > 0 )
					add_del_hna( orig_node, 1 );

				add_del_route( orig_node->orig, 32, orig_node->router, 1, orig_node->batman_if->dev, orig_node->batman_if->udp_send_sock );

			}

			if ( debug_level == 4 )
				output( "Adding new route\n" );


			orig_node->batman_if = max_if;
			add_del_route( orig_node->orig, 32, next_hop->addr, 0, orig_node->batman_if->dev, orig_node->batman_if->udp_send_sock );

			orig_node->router = next_hop->addr;

			/* add new announced network(s) */
			if ( hna_buff_len > 0 ) {

				orig_node->hna_buff = alloc_memory( hna_buff_len );
				orig_node->hna_buff_len = hna_buff_len;

				memcpy( orig_node->hna_buff, hna_recv_buff, hna_buff_len );

				add_del_hna( orig_node, 0 );

			}

		} else if ( ( hna_buff_len != orig_node->hna_buff_len ) || ( ( hna_buff_len > 0 ) && ( orig_node->hna_buff_len > 0 ) && ( memcmp(orig_node->hna_buff, hna_recv_buff, hna_buff_len ) != 0 ) ) ) {

			if ( orig_node->hna_buff_len > 0 )
				add_del_hna( orig_node, 1 );

			if ( hna_buff_len > 0 ) {

				orig_node->hna_buff = alloc_memory( hna_buff_len );
				orig_node->hna_buff_len = hna_buff_len;

				memcpy( orig_node->hna_buff, hna_recv_buff, hna_buff_len );

				add_del_hna( orig_node, 0 );

			}

		}

	}

}

static void update_gw_list( struct orig_node *orig_node, unsigned char new_gwflags )
{

	struct list_head *gw_pos, *gw_pos_tmp;
	struct gw_node *gw_node;
	static char orig_str[ADDR_STR_LEN];

	list_for_each_safe(gw_pos, gw_pos_tmp, &gw_list) {

		gw_node = list_entry(gw_pos, struct gw_node, list);

		if ( gw_node->orig_node == orig_node ) {

			if (debug_level == 3) {

				addr_to_string( gw_node->orig_node->orig, orig_str, ADDR_STR_LEN );
				printf( "Gateway class of originator %s changed from %i to %i\n", orig_str, gw_node->orig_node->gwflags, new_gwflags );

			}

			if ( new_gwflags == 0 ) {

				list_del(gw_pos);
				free_memory(gw_pos);

				if (debug_level == 3)
					printf( "Gateway %s removed from gateway list\n", orig_str );

			} else {

				gw_node->orig_node->gwflags = new_gwflags;

			}

			choose_gw();
			return;

		}

	}

	if ( debug_level == 3 ) {
		addr_to_string( orig_node->orig, orig_str, ADDR_STR_LEN );
		printf( "Found new gateway %s -> class: %i - %s\n", orig_str, new_gwflags, gw2string[new_gwflags] );
	}

	gw_node = alloc_memory(sizeof(struct gw_node));
	memset(gw_node, 0, sizeof(struct gw_node));
	INIT_LIST_HEAD(&gw_node->list);

	gw_node->orig_node = orig_node;
	gw_node->unavail_factor = 0;
	gw_node->last_failure = get_time();

	list_add_tail(&gw_node->list, &gw_list);

	choose_gw();

}



static void debug() {

	struct list_head *forw_pos, *orig_pos, *neigh_pos, *pack_pos;
	struct forw_node *forw_node;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	struct pack_node *pack_node;
	struct gw_node *gw_node;
	unsigned int batman_count = 0;
	static char str[ADDR_STR_LEN], str2[ADDR_STR_LEN];


	if ( ( debug_level == 0 ) || ( debug_level == 3 ) )
		return;

	if ( debug_level != 4 )
		system( "clear" );

	if ( debug_level == 2 ) {

		if ( list_empty(&gw_list) ) {

			printf( "No gateways in range ...\n" );

		} else {

			list_for_each(orig_pos, &gw_list) {
				gw_node = list_entry(orig_pos, struct gw_node, list);

				addr_to_string( gw_node->orig_node->orig, str, sizeof (str) );
				addr_to_string( gw_node->orig_node->router, str2, sizeof (str2) );

				if ( curr_gateway == gw_node ) {
					printf( "=> %s via: %s(%i), gw_class %i - %s, reliability: %i\n", str, str2, gw_node->orig_node->packet_count, gw_node->orig_node->gwflags, gw2string[gw_node->orig_node->gwflags], gw_node->unavail_factor );
				} else {
					printf( "%s via: %s(%i), gw_class %i - %s, reliability: %i\n", str, str2, gw_node->orig_node->packet_count, gw_node->orig_node->gwflags, gw2string[gw_node->orig_node->gwflags], gw_node->unavail_factor );
				}

			}

		}

	} else {

		if ( debug_level == 4 ) {

			output( "------------------ DEBUG ------------------\n" );
			output( "Forward list\n" );

			list_for_each( forw_pos, &forw_list ) {
				forw_node = list_entry( forw_pos, struct forw_node, list );
				addr_to_string( ((struct packet *)forw_node->pack_buff)->orig, str, sizeof (str) );
				output( "    %s at %u\n", str, forw_node->when );
			}

			output( "Originator list\n" );

		}

		list_for_each(orig_pos, &orig_list) {
			orig_node = list_entry(orig_pos, struct orig_node, list);

			if ( orig_node->router == 0 )
				continue;

			batman_count++;

			addr_to_string( orig_node->orig, str, sizeof (str) );
			addr_to_string( orig_node->router, str2, sizeof (str2) );

			if ( debug_level != 4 ) {
				printf( "%s, GW: %s(%i) via:", str, str2, orig_node->packet_count );
			} else {
				output( "%s, GW: %s(%i), last_aware:%u, last_reply:%u, last_seen:%u via:\n", str, str2, orig_node->packet_count, orig_node->last_aware, orig_node->last_reply, orig_node->last_seen );
			}

			list_for_each(neigh_pos, &orig_node->neigh_list) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				addr_to_string(neigh_node->addr, str, sizeof (str));

				if ( debug_level != 4 ) {
					printf( " %s(%i)", str, neigh_node->packet_count );
				} else {
					output( "\t\t%s (%d)\n", str, neigh_node->packet_count );
				}

				if ( debug_level == 4 ) {

					list_for_each(pack_pos, &neigh_node->pack_list) {
						pack_node = list_entry(pack_pos, struct pack_node, list);
						output("        Sequence number: %d, TTL: %d at: %u \n",
								pack_node->seqno, pack_node->ttl, pack_node->time);
					}

				}

			}

			if ( debug_level != 4 )
				printf( "\n" );

		}

		if ( batman_count == 0 ) {

			if ( debug_level != 4 ) {
				printf( "No batman nodes in range ...\n" );
			} else {
				output( "No batman nodes in range ...\n" );
			}

		}

		if ( debug_level == 4 )
			output( "---------------------------------------------- END DEBUG\n" );

	}

}



int isDuplicate(unsigned int orig, unsigned short seqno)
{
	struct list_head *orig_pos, *neigh_pos, *pack_pos;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	struct pack_node *pack_node;

	list_for_each(orig_pos, &orig_list) {
		orig_node = list_entry(orig_pos, struct orig_node, list);

		if ( orig == orig_node->orig ) {

			list_for_each(neigh_pos, &orig_node->neigh_list) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				list_for_each(pack_pos, &neigh_node->pack_list) {
					pack_node = list_entry(pack_pos, struct pack_node, list);

					if (orig_node->orig == orig && pack_node->seqno == seqno){
						return 1;
					}

				}

			}

		}

	}

	return 0;
}

int isBidirectionalNeigh( struct orig_node *orig_neigh_node, struct batman_if *if_incoming ) {

	if( orig_neigh_node->last_reply[if_incoming->if_num] > 0 && (orig_neigh_node->last_reply[if_incoming->if_num] + (bidirectional_timeout)) >= get_time() )
		return 1;
	else
		return 0;

}

int hasUnidirectionalFlag( struct packet *in )
{
	if( in->flags & UNIDIRECTIONAL )
		return 1;
	else return 0;
}



struct orig_node *update_last_hop(struct packet *in, unsigned int neigh)
{
	struct orig_node *orig_neigh_node;

	if (debug_level == 4) {
		output("update_last_hop(): Searching originator entry of last-hop neighbour of received packet \n"); }
	orig_neigh_node = get_orig_node( neigh );

	orig_neigh_node->last_aware = get_time();

	return orig_neigh_node;

}

void update_originator( struct packet *in, unsigned int neigh, struct batman_if *if_incoming, unsigned char *hna_recv_buff, int hna_buff_len ) {

	struct list_head *neigh_pos, *pack_pos;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node = NULL;
	struct pack_node *pack_node = NULL;

	if (debug_level == 4)
		output("update_originator(): Searching and updating originator entry of received packet,  \n");

	orig_node = get_orig_node( in->orig );

	orig_node->last_seen = get_time();
	orig_node->flags = in->flags;

	if ( orig_node->gwflags != in->gwflags )
		update_gw_list( orig_node, in->gwflags );

	orig_node->gwflags = in->gwflags;

	list_for_each(neigh_pos, &orig_node->neigh_list) {
		neigh_node = list_entry(neigh_pos, struct neigh_node, list);

		if (neigh_node->addr != neigh)
			neigh_node = NULL;
	}

	if (neigh_node == NULL)  {
		if (debug_level == 4)
			output("Creating new last-hop neighbour of originator\n");

		neigh_node = alloc_memory(sizeof (struct neigh_node));
		INIT_LIST_HEAD(&neigh_node->list);
		INIT_LIST_HEAD(&neigh_node->pack_list);

		neigh_node->addr = neigh;

		list_add_tail(&neigh_node->list, &orig_node->neigh_list);
	} else if (debug_level == 4)
		output("Updating existing last-hop neighbour of originator\n");

	list_for_each(pack_pos, &neigh_node->pack_list) {
		pack_node = list_entry(pack_pos, struct pack_node, list);

		if (pack_node->seqno != in->seqno)
			pack_node = NULL;
	}

	if (pack_node == NULL)  {
		if (debug_level == 4)
			output("Creating new packet entry for last-hop neighbour of originator \n");

		pack_node = alloc_memory(sizeof (struct pack_node));
		INIT_LIST_HEAD(&pack_node->list);

		pack_node->seqno = in->seqno;
		pack_node->if_incoming = if_incoming;
		list_add_tail(&pack_node->list, &neigh_node->pack_list);
	} else
		if (debug_level == 4)
			output("ERROR - Updating existing packet\n");

	neigh_node->best_ttl = in->ttl;
	pack_node->ttl = in->ttl;
	pack_node->time = get_time();

	update_routes( orig_node, hna_recv_buff, hna_buff_len );

}

void schedule_forward_packet( struct packet *in, int unidirectional, int directlink, struct orig_node *orig_node, unsigned int neigh, unsigned char *hna_recv_buff, int hna_buff_len, struct batman_if *if_outgoing ) {

	struct forw_node *forw_node_new;

	if ( debug_level == 4 )
		output( "schedule_forward_packet():  \n" );

	if ( in->ttl <= 1 ) {

		if ( debug_level == 4 )
			output( "ttl exceeded \n" );

	} else {

		forw_node_new = alloc_memory( sizeof(struct forw_node) );

		INIT_LIST_HEAD(&forw_node_new->list);

		if ( hna_buff_len > 0 ) {

			forw_node_new->pack_buff = alloc_memory( sizeof(struct packet) + hna_buff_len );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct packet) );
			memcpy( forw_node_new->pack_buff + sizeof(struct packet), hna_recv_buff, hna_buff_len );
			forw_node_new->pack_buff_len = sizeof(struct packet) + hna_buff_len;

		} else {

			forw_node_new->pack_buff = alloc_memory( sizeof(struct packet) );
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

			if ( debug_level == 4 )
				addr_to_string( ((struct packet *)forw_node->pack_buff)->orig, orig_str, ADDR_STR_LEN );

			directlink = ( ( ((struct packet *)forw_node->pack_buff)->flags & DIRECTLINK ) ? 1 : 0 );


			if ( ((struct packet *)forw_node->pack_buff)->flags & UNIDIRECTIONAL ) {

				if ( forw_node->if_outgoing != NULL ) {

					if ( debug_level == 4 )
						output( "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ((struct packet *)forw_node->pack_buff)->seqno, ((struct packet *)forw_node->pack_buff)->ttl, forw_node->if_outgoing->dev );

					if ( send_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 ) {
						exit( -1 );
					}

				} else {

					do_log( "Error - can't forward packet with UDF: outgoing iface not specified \n", "" );

				}

			/* multihomed peer assumed */
			} else if ( ( directlink ) && ( ((struct packet *)forw_node->pack_buff)->ttl == 1 ) ) {

				if ( ( forw_node->if_outgoing != NULL ) ) {

					if ( send_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 ) {
						exit( -1 );
					}

				} else {

					do_log( "Error - can't forward packet with IDF: outgoing iface not specified (multihomed) \n", "" );

				}

			} else {

				if ( ( directlink ) && ( forw_node->if_outgoing == NULL ) ) {

					do_log( "Error - can't forward packet with IDF: outgoing iface not specified \n", "" );

				} else {

					list_for_each(if_pos, &if_list) {

						batman_if = list_entry(if_pos, struct batman_if, list);

						if ( ( directlink ) && ( forw_node->if_outgoing == batman_if ) ) {
							((struct packet *)forw_node->pack_buff)->flags = DIRECTLINK;
						} else {
							((struct packet *)forw_node->pack_buff)->flags = 0x00;
						}

						if ( debug_level == 4 )
							output( "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ((struct packet *)forw_node->pack_buff)->seqno, ((struct packet *)forw_node->pack_buff)->ttl, batman_if->dev );

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

			free_memory( forw_node->pack_buff );
			free_memory( forw_node );

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

			forw_node_new = alloc_memory( sizeof(struct forw_node) );

			INIT_LIST_HEAD(&forw_node_new->list);

			forw_node_new->when = curr_time + rand_num( JITTER );
			forw_node_new->if_outgoing = NULL;
			forw_node_new->own = 1;

			if ( num_hna > 0 ) {

				forw_node_new->pack_buff = alloc_memory( sizeof(struct packet) + num_hna * 5 * sizeof(unsigned char) );
				memcpy( forw_node_new->pack_buff, (unsigned char *)&batman_if->out, sizeof(struct packet) );
				memcpy( forw_node_new->pack_buff + sizeof(struct packet), hna_buff, num_hna * 5 * sizeof(unsigned char) );
				forw_node_new->pack_buff_len = sizeof(struct packet) + num_hna * 5 * sizeof(unsigned char);

			} else {

				forw_node_new->pack_buff = alloc_memory( sizeof(struct packet) );
				memcpy( forw_node_new->pack_buff, &batman_if->out, sizeof(struct packet) );
				forw_node_new->pack_buff_len = sizeof(struct packet);

			}

			list_add( &forw_node_new->list, &forw_list );

			batman_if->out.seqno++;

		}

		last_own_packet = curr_time;

	}

}

void purge()
{
	struct list_head *orig_pos, *neigh_pos, *pack_pos, *gw_pos, *gw_pos_tmp, *orig_temp, *neigh_temp, *pack_temp;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	struct pack_node *pack_node;
	struct gw_node *gw_node;
	int gw_purged = 0, purged_packets;
	static char orig_str[ADDR_STR_LEN], neigh_str[ADDR_STR_LEN];

	if (debug_level == 4)
		output("purge() \n");

	/* for all origins... */
	list_for_each_safe(orig_pos, orig_temp, &orig_list) {
		orig_node = list_entry(orig_pos, struct orig_node, list);

		purged_packets = 0;

		/* for all neighbours towards the origins... */
		list_for_each_safe(neigh_pos, neigh_temp, &orig_node->neigh_list) {
			neigh_node = list_entry(neigh_pos, struct neigh_node, list);

			/* for all packets from the origins via this neighbours... */
			list_for_each_safe(pack_pos, pack_temp, &neigh_node->pack_list) {
				pack_node = list_entry(pack_pos, struct pack_node, list);

				/* remove them if outdated */
				if ((int)((pack_node->time + TIMEOUT) < get_time()))
				{
					if (debug_level == 4) {
						addr_to_string(orig_node->orig, orig_str, ADDR_STR_LEN);
						addr_to_string(neigh_node->addr, neigh_str, ADDR_STR_LEN);
						output("Packet timeout (originator %s, neighbour %s, seqno %d, TTL %d, time %u)\n",
						     orig_str, neigh_str, pack_node->seqno, pack_node->ttl, pack_node->time);
					}

					purged_packets++;
					list_del(pack_pos);
					free_memory(pack_node);

				} else {

					/* if this packet is not outdated the following packets won't be either */
					break;

				}

			}

			/* if no more packets, remove neighbour (next hop) towards given origin */
			if (list_empty(&neigh_node->pack_list)) {
				if (debug_level == 4) {
					addr_to_string(neigh_node->addr, neigh_str, sizeof (neigh_str));
					addr_to_string(orig_node->orig, orig_str, sizeof (orig_str));
					output("Removing orphaned neighbour %s for originator %s\n", neigh_str, orig_str);
				}
				list_del(neigh_pos);
				free_memory(neigh_node);
			}
		}

		/* if no more neighbours (next hops) towards given origin, remove origin */
		if (list_empty(&orig_node->neigh_list) && ((int)(orig_node->last_aware) + TIMEOUT <= ((int)(get_time())))) {

			if (debug_level == 4) {
				addr_to_string(orig_node->orig, orig_str, sizeof (orig_str));
				output("Removing orphaned originator %s\n", orig_str);
			}

			list_for_each_safe(gw_pos, gw_pos_tmp, &gw_list) {

				gw_node = list_entry(gw_pos, struct gw_node, list);

				if ( gw_node->orig_node == orig_node ) {

					addr_to_string( gw_node->orig_node->orig, orig_str, ADDR_STR_LEN );
					if (debug_level == 3)
						printf( "Removing gateway %s from gateway list\n", orig_str );

					list_del(gw_pos);
					free_memory(gw_pos);

					gw_purged = 1;

					break;

				}

			}

			list_del(orig_pos);

			/* remove old announced network(s) */
			if ( orig_node->hna_buff_len > 0 ) {

				add_del_hna( orig_node, 1 );
				free_memory( orig_node->hna_buff );

			}

			if ( orig_node->router != 0 ) {

				if (debug_level == 4)
					output("Deleting route to originator \n");

				add_del_route(orig_node->orig, 32, orig_node->router, 1, orig_node->batman_if->dev, orig_node->batman_if->udp_send_sock);

			}

			free_memory( orig_node->last_reply );
			free_memory( orig_node );

		} else if ( purged_packets > 0 ) {

			/* update packet count of orginator */
			update_routes( orig_node, orig_node->hna_buff, orig_node->hna_buff_len );

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
		if(orig_node->orig == orig_node->router)
		{
			if(cnt >= size)
			{
				size += step;
				packet = realloc_memory(packet, size * sizeof(unsigned char));
			}
			memmove(&packet[cnt], (unsigned char*)&orig_node->orig,4);
			 *(packet + cnt + 4) = (unsigned char) orig_node->packet_count;
			cnt += step;
		}
	}
	if(packet != NULL)
	{
		send_packet(packet, size * sizeof(unsigned char), &vis_if.addr, vis_if.sock);
	 	free_memory(packet);
	}
}

int batman()
{
	struct list_head *orig_pos, *if_pos, *neigh_pos, *hna_pos;
	struct orig_node *orig_node, *orig_neigh_node;
	struct batman_if *batman_if, *if_incoming;
	struct neigh_node *neigh_node;
	struct hna_node *hna_node;
	unsigned int neigh, hna, netmask, debug_timeout, select_timeout;
	unsigned char in[1501], *hna_recv_buff;
	static char orig_str[ADDR_STR_LEN], neigh_str[ADDR_STR_LEN];
	int forward_old, res, hna_buff_len, hna_buff_count;
	int is_my_addr, is_my_orig, is_broadcast, is_duplicate, is_bidirectional, forward_duplicate_packet;
	int time_count = 0, curr_time;

	last_own_packet = debug_timeout = get_time();
	bidirectional_timeout = orginator_interval * 3;

	if ( !( list_empty(&hna_list) ) ) {

		list_for_each(hna_pos, &hna_list) {

			hna_node = list_entry(hna_pos, struct hna_node, list);

			hna_buff = realloc_memory( hna_buff, ( num_hna + 1 ) * 5 * sizeof( unsigned char ) );

			memmove( &hna_buff[ num_hna * 5 ], ( unsigned char *)&hna_node->addr, 4 );
			hna_buff[ ( num_hna * 5 ) + 4 ] = ( unsigned char ) hna_node->netmask;

			num_hna++;

		}

	}

	list_for_each(if_pos, &if_list) {

		batman_if = list_entry(if_pos, struct batman_if, list);

		batman_if->out.orig = batman_if->addr.sin_addr.s_addr;
		batman_if->out.flags = 0x00;
		batman_if->out.ttl = ( batman_if->if_num == 0 ? TTL : 2 );
		batman_if->out.seqno = 0;
		batman_if->out.gwflags = gateway_class;
		batman_if->out.version = BATMAN_VERSION;

	}

	forward_old = get_forwarding();
	set_forwarding(1);

	while (!is_aborted())
	{
		if (debug_level == 4)
			output(" \n \n");

		if(vis_if.sock && time_count == 50)
		{
			time_count = 0;
			send_vis_packet();
		}

		/* harden select_timeout against sudden time change (e.g. ntpdate) */
		curr_time = get_time();
		select_timeout = ( curr_time >= last_own_packet + orginator_interval - 10 ? orginator_interval : last_own_packet + orginator_interval - curr_time );

		res = receive_packet( ( unsigned char *)&in, 1501, &hna_buff_len, &neigh, select_timeout, &if_incoming );

		if (res < 0)
			return -1;

		if (res > 0)
		{
			if ( debug_level == 4 )  {
				addr_to_string( ((struct packet *)&in)->orig, orig_str, sizeof(orig_str) );
				addr_to_string( neigh, neigh_str, sizeof(neigh_str) );
				output( "Received BATMAN packet from %s (originator %s, seqno %d, TTL %d)\n", neigh_str, orig_str, ((struct packet *)&in)->seqno, ((struct packet *)&in)->ttl );
			}

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


			if ( debug_level == 4 ) {

				addr_to_string( ((struct packet *)&in)->orig, orig_str, sizeof (orig_str) );
				addr_to_string( neigh, neigh_str, sizeof (neigh_str) );
				output( "new packet - orig: %s, sender: %s\n",orig_str , neigh_str );

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
					output( "Is an internet gateway (class %i) \n", ((struct packet *)&in)->gwflags );

				if ( hna_buff_len > 4 ) {

					output( "HNA information received (%i HNA network%s):\n", hna_buff_len / 5, ( hna_buff_len / 5 > 1 ? "s": "" ) );
					hna_buff_count = 0;

					while ( ( hna_buff_count + 1 ) * 5 <= hna_buff_len ) {

						memmove( &hna, ( unsigned int *)&hna_recv_buff[ hna_buff_count * 5 ], 4 );
						netmask = ( unsigned int )hna_recv_buff[ ( hna_buff_count * 5 ) + 4 ];

						addr_to_string( hna, orig_str, sizeof (orig_str) );

						if ( ( netmask > 0 ) && ( netmask < 33 ) )
							output( "hna: %s/%i\n", orig_str, netmask );
						else
							output( "hna: %s/%i -> ignoring (invalid netmask)\n", orig_str, netmask );

						hna_buff_count++;

					}

				}

			}


			if ( ((struct packet *)&in)->version != BATMAN_VERSION ) {

				if ( debug_level == 4 )
					output( "Drop packet: incompatible batman version (%i) \n", ((struct packet *)&in)->version );

			} else if ( is_my_addr ) {

				if ( debug_level == 4 ) {
					addr_to_string( neigh, neigh_str, sizeof (neigh_str) );
					output( "Drop packet: received my own broadcast (sender: %s)\n", neigh_str );
				}

			} else if ( is_broadcast ) {

				if ( debug_level == 4 ) {
					addr_to_string( neigh, neigh_str, sizeof (neigh) );
					output( "Drop packet: ignoring all packets with broadcast source IP (sender: %s)\n", neigh_str );
				}

			} else if ( is_my_orig ) {

				orig_neigh_node = update_last_hop( (struct packet *)&in, neigh );

				/* neighbour has to indicating direct link and it has to come via the corresponding interface */
				if ( ( ((struct packet *)&in)->flags & DIRECTLINK ) && ( if_incoming->addr.sin_addr.s_addr == ((struct packet *)&in)->orig ) ) {

					orig_neigh_node->last_reply[if_incoming->if_num] = get_time();

					if ( debug_level == 4 )
						output( "received my own packet from neighbour indicating bidirectional link, updating last_reply stamp \n");

				}

				if ( debug_level == 4 )
					output( "Drop packet: originator packet from myself (via neighbour) \n" );

			} else if ( ((struct packet *)&in)->flags & UNIDIRECTIONAL ) {

				if ( debug_level == 4 )
					output( "Drop packet: originator packet with unidirectional flag \n" );

			} else {

				orig_neigh_node = update_last_hop( (struct packet *)&in, neigh );

				is_duplicate = isDuplicate( ((struct packet *)&in)->orig, ((struct packet *)&in)->seqno );
				is_bidirectional = isBidirectionalNeigh( orig_neigh_node, if_incoming );

				/* update ranking */
				if ( ( is_bidirectional ) && ( !is_duplicate ) )
					update_originator( (struct packet *)&in, neigh, if_incoming, hna_recv_buff, hna_buff_len );

				/* is single hop (direct) neighbour */
				if ( ((struct packet *)&in)->orig == neigh ) {

					/* it is our best route towards him */
					if ( ( is_bidirectional ) && ( orig_neigh_node->router == neigh ) ) {

						/* mark direct link on incoming interface */
						schedule_forward_packet( (struct packet *)&in, 0, 1, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

						if ( debug_level == 4 )
							output( "Forward packet: rebroadcast neighbour packet with direct link flag \n" );

					/* if an unidirectional neighbour sends us a packet - retransmit it with unidirectional flag to tell him that we get its packets */
					/* if a bidirectional neighbour sends us a packet - retransmit it with unidirectional flag if it is not our best link to it in order to prevent routing problems */
					} else if ( ( ( is_bidirectional ) && ( orig_neigh_node->router != neigh ) ) || ( !is_bidirectional ) ) {

						schedule_forward_packet( (struct packet *)&in, 1, 1, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

						if ( debug_level == 4 )
							output( "Forward packet: rebroadcast neighbour packet with direct link and unidirectional flag \n" );

					}

				/* multihop orginator */
				} else {

					if ( is_bidirectional ) {

						if ( !is_duplicate ) {

							schedule_forward_packet( (struct packet *)&in, 0, 0, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

							if ( debug_level == 4 )
								output( "Forward packet: rebroadcast orginator packet \n" );

						} else if ( orig_neigh_node->router == neigh ) {

							list_for_each(neigh_pos, &orig_neigh_node->neigh_list) {

								neigh_node = list_entry(neigh_pos, struct neigh_node, list);

								if ( neigh_node->addr == neigh ) {

									if ( neigh_node->best_ttl == ((struct packet *)&in)->ttl )
										forward_duplicate_packet = 1;

									break;

								}

							}

							/* we are forwarding duplicate o-packets if they come via our best neighbour and ttl is valid */
							if ( forward_duplicate_packet ) {

								schedule_forward_packet( (struct packet *)&in, 0, 0, orig_neigh_node, neigh, hna_recv_buff, hna_buff_len, if_incoming );

								if ( debug_level == 4 )
									output( "Forward packet: duplicate packet received via best neighbour with best ttl \n" );

							} else {

								if ( debug_level == 4 )
									output( "Drop packet: duplicate packet received via best neighbour but not best ttl \n" );

							}

						} else {

							if ( debug_level == 4 )
								output( "Drop packet: duplicate packet (not received via best neighbour) \n" );

						}

					} else {

						if ( debug_level == 4 )
							output( "Drop packet: received via unidirectional link \n" );

					}

				}

			}

		}

		schedule_own_packet();

		send_outstanding_packets();

		if ( ( routing_class != 0 ) && ( curr_gateway == NULL ) )
			choose_gw();

		purge();

		if ( debug_timeout + 1000 < get_time() ) {

			debug_timeout = get_time();
			debug();

		}

		time_count++;

	}

	if ( debug_level > 0 )
		printf( "Deleting all BATMAN routes\n" );

	list_for_each(orig_pos, &orig_list) {

		orig_node = list_entry(orig_pos, struct orig_node, list);

		/* remove old announced network(s) */
		if ( orig_node->hna_buff_len > 0 )
			add_del_hna( orig_node, 1 );

		if ( orig_node->router != 0 )
			add_del_route( orig_node->orig, 32, orig_node->router, 1, orig_node->batman_if->dev, batman_if->udp_send_sock );

	}

	set_forwarding( forward_old );

	return 0;

}
