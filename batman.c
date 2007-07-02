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
#include "batman.h"
#include "originator.h"
#include "schedule.h"

#include <errno.h>  /* should be removed together with tcp control channel */



uint8_t debug_level = 0;


#ifdef PROFILE_DATA

uint8_t debug_level_max = 5;

#elif DEBUG_MALLOC && MEMORY_USAGE

uint8_t debug_level_max = 5;

#else

uint8_t debug_level_max = 4;

#endif


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

uint8_t gateway_class = 0;

/* "-r" is the command line switch for the routing class,
 * 0 set no default route
 * 1 use fast internet connection
 * 2 use stable internet connection
 * 3 use use best statistic (olsr style)
 * this option is used to set the routing behaviour
 */

uint8_t routing_class = 0;


int16_t originator_interval = 1000;   /* originator message interval in miliseconds */

struct gw_node *curr_gateway = NULL;
pthread_t curr_gateway_thread_id = 0;

uint32_t pref_gateway = 0;

unsigned char *hna_buff = NULL;

uint8_t num_hna = 0;

uint8_t found_ifs = 0;
int32_t receive_max_sock = 0;
fd_set receive_wait_set;

uint8_t unix_client = 0;

struct hashtable_t *orig_hash;

struct list_head_first forw_list;
struct list_head_first gw_list;
struct list_head_first if_list;
struct list_head_first hna_list;

struct vis_if vis_if;
struct unix_if unix_if;
struct debug_clients debug_clients;

unsigned char *vis_packet = NULL;
uint16_t vis_packet_size = 0;



void usage( void ) {

	fprintf( stderr, "Usage: batman [options] interface [interface interface]\n" );
	fprintf( stderr, "       -a announce network(s)\n" );
	fprintf( stderr, "       -b run connection in batch mode\n" );
	fprintf( stderr, "       -c connect via unix socket\n" );
	fprintf( stderr, "       -d debug level\n" );
	fprintf( stderr, "       -g gateway class\n" );
	fprintf( stderr, "       -h this help\n" );
	fprintf( stderr, "       -H verbose help\n" );
	fprintf( stderr, "       -o originator interval in ms\n" );
	fprintf( stderr, "       -p preferred gateway\n" );
	fprintf( stderr, "       -r routing class\n" );
	fprintf( stderr, "       -s visualisation server\n" );
	fprintf( stderr, "       -v print version\n" );

}



void verbose_usage( void ) {

	fprintf( stderr, "Usage: batman [options] interface [interface interface]\n\n" );
	fprintf( stderr, "       -a announce network(s)\n" );
	fprintf( stderr, "          network/netmask is expected\n" );
	fprintf( stderr, "       -b run connection in batch mode\n" );
	fprintf( stderr, "       -c connect to running batmand via unix socket\n" );
	fprintf( stderr, "       -d debug level\n" );
	fprintf( stderr, "          default:         0 -> debug disabled\n" );
	fprintf( stderr, "          allowed values:  1 -> list neighbours\n" );
	fprintf( stderr, "                           2 -> list gateways\n" );
	fprintf( stderr, "                           3 -> observe batman\n" );
	fprintf( stderr, "                           4 -> observe batman (very verbose)\n\n" );
	fprintf( stderr, "       -g gateway class\n" );
	fprintf( stderr, "          default:         0 -> this is not an internet gateway\n" );
	fprintf( stderr, "          allowed values:  1 -> modem line\n" );
	fprintf( stderr, "                           2 -> ISDN line\n" );
	fprintf( stderr, "                           3 -> double ISDN\n" );
	fprintf( stderr, "                           4 -> 256 KBit\n" );
	fprintf( stderr, "                           5 -> UMTS / 0.5 MBit\n" );
	fprintf( stderr, "                           6 -> 1 MBit\n" );
	fprintf( stderr, "                           7 -> 2 MBit\n" );
	fprintf( stderr, "                           8 -> 3 MBit\n" );
	fprintf( stderr, "                           9 -> 5 MBit\n" );
	fprintf( stderr, "                          10 -> 6 MBit\n" );
	fprintf( stderr, "                          11 -> >6 MBit\n\n" );
	fprintf( stderr, "       -h shorter help\n" );
	fprintf( stderr, "       -H this help\n" );
	fprintf( stderr, "       -o originator interval in ms\n" );
	fprintf( stderr, "          default: 1000, allowed values: >0\n\n" );
	fprintf( stderr, "       -p preferred gateway\n" );
	fprintf( stderr, "          default: none, allowed values: IP\n\n" );
	fprintf( stderr, "       -r routing class (only needed if gateway class = 0)\n" );
	fprintf( stderr, "          default:         0 -> set no default route\n" );
	fprintf( stderr, "          allowed values:  1 -> use fast internet connection\n" );
	fprintf( stderr, "                           2 -> use stable internet connection\n" );
	fprintf( stderr, "                           3 -> use best statistic internet connection (olsr style)\n\n" );
	fprintf( stderr, "       -s visualisation server\n" );
	fprintf( stderr, "          default: none, allowed values: IP\n\n" );
	fprintf( stderr, "       -v print version\n" );

}



int is_batman_if( char *dev, struct batman_if **batman_if ) {

	struct list_head *if_pos;


	list_for_each( if_pos, &if_list ) {

		(*batman_if) = list_entry( if_pos, struct batman_if, list );

		if ( strcmp( (*batman_if)->dev, dev ) == 0 )
			return 1;

	}

	return 0;

}



void add_del_hna( struct orig_node *orig_node, int8_t del ) {

	uint16_t hna_buff_count = 0;
	uint32_t hna, netmask;

	while ( ( hna_buff_count + 1 ) * 5 <= orig_node->hna_buff_len ) {

		memcpy( &hna, ( uint32_t *)&orig_node->hna_buff[ hna_buff_count * 5 ], 4 );
		netmask = ( uint32_t )orig_node->hna_buff[ ( hna_buff_count * 5 ) + 4 ];

		if ( ( netmask > 0 ) && ( netmask < 33 ) ) {

			add_del_route( hna, netmask, orig_node->router->addr, orig_node->batman_if->if_index, orig_node->batman_if->dev, BATMAN_RT_TABLE_NETWORKS, 0, del );
			add_del_rule( hna, netmask, BATMAN_RT_TABLE_NETWORKS, 0, 0, 1, del );

		}

		hna_buff_count++;

	}

	if ( del ) {

		debugFree( orig_node->hna_buff, 1101 );
		orig_node->hna_buff_len = 0;

	}

}



void choose_gw() {

	prof_start( PROF_choose_gw );
	struct list_head *pos;
	struct gw_node *gw_node, *tmp_curr_gw = NULL;
	uint8_t max_gw_class = 0, max_packets = 0, max_gw_factor = 0;
	static char orig_str[ADDR_STR_LEN];


	if ( routing_class == 0 ) {

		prof_stop( PROF_choose_gw );
		return;

	}

	if ( list_empty(&gw_list) ) {

		if ( curr_gateway != NULL ) {

			debug_output( 3, "Removing default route - no gateway in range\n" );

			del_default_route();

		}

		prof_stop( PROF_choose_gw );
		return;

	}


	list_for_each(pos, &gw_list) {

		gw_node = list_entry(pos, struct gw_node, list);

		/* ignore this gateway if recent connection attempts were unsuccessful */
		if ( ( gw_node->unavail_factor * gw_node->unavail_factor * 30000 ) + gw_node->last_failure > get_time() )
			continue;

		if ( gw_node->orig_node->router == NULL )
			continue;

		if ( gw_node->deleted )
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

			if ( tmp_curr_gw != NULL )
				debug_output( 3, "Removing default route - better gateway found\n" );
			else
				debug_output( 3, "Removing default route - no gateway in range\n" );

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

	prof_stop( PROF_choose_gw );

}



void update_routes( struct orig_node *orig_node, struct neigh_node *neigh_node, unsigned char *hna_recv_buff, int16_t hna_buff_len ) {

	prof_start( PROF_update_routes );
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

			add_del_route( orig_node->orig, 32, orig_node->router->addr, orig_node->batman_if->if_index, orig_node->batman_if->dev, BATMAN_RT_TABLE_HOSTS, 0, 1 );

		}

		/* route altered or new route added */
		if ( ( ( orig_node->router != NULL ) && ( neigh_node != NULL ) ) || ( orig_node->router == NULL ) ) {

			if ( orig_node->router == NULL ) {
				debug_output( 4, "Adding new route\n" );
			} else {
				debug_output( 4, "Route changed\n" );
			}

			add_del_route( orig_node->orig, 32, neigh_node->addr, neigh_node->if_incoming->if_index, neigh_node->if_incoming->dev, BATMAN_RT_TABLE_HOSTS, 0, 0 );

			orig_node->batman_if = neigh_node->if_incoming;
			orig_node->router = neigh_node;

			/* add new announced network(s) */
			if ( hna_buff_len > 0 ) {

				orig_node->hna_buff = debugMalloc( hna_buff_len, 101 );
				orig_node->hna_buff_len = hna_buff_len;

				memmove( orig_node->hna_buff, hna_recv_buff, hna_buff_len );

				add_del_hna( orig_node, 0 );

			}

		}

		orig_node->router = neigh_node;

	} else if ( orig_node != NULL ) {

		/* may be just HNA changed */
		if ( ( hna_buff_len != orig_node->hna_buff_len ) || ( ( hna_buff_len > 0 ) && ( orig_node->hna_buff_len > 0 ) && ( memcmp( orig_node->hna_buff, hna_recv_buff, hna_buff_len ) != 0 ) ) ) {

			if ( orig_node->hna_buff_len > 0 )
				add_del_hna( orig_node, 1 );

			if ( hna_buff_len > 0 ) {

				orig_node->hna_buff = debugMalloc( hna_buff_len, 102 );
				orig_node->hna_buff_len = hna_buff_len;

				memcpy( orig_node->hna_buff, hna_recv_buff, hna_buff_len );

				add_del_hna( orig_node, 0 );

			}

		}

	}

	prof_stop( PROF_update_routes );

}



void update_gw_list( struct orig_node *orig_node, uint8_t new_gwflags ) {

	prof_start( PROF_update_gw_list );
	struct list_head *gw_pos, *gw_pos_tmp;
	struct gw_node *gw_node;
	static char orig_str[ADDR_STR_LEN];

	list_for_each_safe( gw_pos, gw_pos_tmp, &gw_list ) {

		gw_node = list_entry(gw_pos, struct gw_node, list);

		if ( gw_node->orig_node == orig_node ) {

			addr_to_string( gw_node->orig_node->orig, orig_str, ADDR_STR_LEN );
			debug_output( 3, "Gateway class of originator %s changed from %i to %i\n", orig_str, gw_node->orig_node->gwflags, new_gwflags );

			if ( new_gwflags == 0 ) {

				gw_node->deleted = get_time();

				debug_output( 3, "Gateway %s removed from gateway list\n", orig_str );

			} else {

				gw_node->deleted = 0;
				gw_node->orig_node->gwflags = new_gwflags;

			}

			prof_stop( PROF_update_gw_list );
			choose_gw();
			return;

		}

	}

	addr_to_string( orig_node->orig, orig_str, ADDR_STR_LEN );
	debug_output( 3, "Found new gateway %s -> class: %i - %s\n", orig_str, new_gwflags, gw2string[new_gwflags] );

	gw_node = debugMalloc( sizeof(struct gw_node), 103 );
	memset( gw_node, 0, sizeof(struct gw_node) );
	INIT_LIST_HEAD( &gw_node->list );

	gw_node->orig_node = orig_node;
	gw_node->unavail_factor = 0;
	gw_node->last_failure = get_time();

	list_add_tail( &gw_node->list, &gw_list );

	prof_stop( PROF_update_gw_list );

	choose_gw();

}



int isDuplicate( struct orig_node *orig_node, uint16_t seqno ) {

	prof_start( PROF_is_duplicate );
	struct list_head *neigh_pos;
	struct neigh_node *neigh_node;

	list_for_each( neigh_pos, &orig_node->neigh_list ) {

		neigh_node = list_entry( neigh_pos, struct neigh_node, list );

		if ( get_bit_status( neigh_node->seq_bits, orig_node->last_seqno, seqno ) ) {

			prof_stop( PROF_is_duplicate );
			return 1;

		}

	}

	prof_stop( PROF_is_duplicate );

	return 0;

}

int isBntog( uint32_t neigh, struct orig_node *orig_tog_node ) {

	if ( ( orig_tog_node->router != NULL ) && ( orig_tog_node->router->addr == neigh ) )
		return 1;

	return 0;

}

int isBidirectionalNeigh( struct orig_node *orig_neigh_node, struct batman_if *if_incoming ) {

	if ( ( if_incoming->out.bat_packet.seqno - 2 - orig_neigh_node->bidirect_link[if_incoming->if_num] ) < BIDIRECT_TIMEOUT )
		return 1;

	return 0;

}



void generate_vis_packet() {

	struct hash_it_t *hashit = NULL;
	struct orig_node *orig_node;


	if ( vis_packet != NULL )
		debugFree( vis_packet, 1102 );

	/* sender ip and gateway class */
	vis_packet_size = 6;
	vis_packet = debugMalloc( vis_packet_size, 104 );

	memcpy( vis_packet, (unsigned char *)&(((struct batman_if *)if_list.next)->addr.sin_addr.s_addr), 4 );
	vis_packet[4] = gateway_class;
	vis_packet[5] = SEQ_RANGE;


	while ( NULL != ( hashit = hash_iterate( orig_hash, hashit ) ) ) {

		orig_node = hashit->bucket->data;

		/* we interested in 1 hop neighbours only */
		if ( ( orig_node->router != NULL ) && ( orig_node->orig == orig_node->router->addr ) ) {

			/* neighbour ip and packet count */
			vis_packet_size += 5;

			vis_packet = debugRealloc( vis_packet, vis_packet_size, 105 );

			memcpy( vis_packet + vis_packet_size - 5, (unsigned char *)&orig_node->orig, 4 );

			vis_packet[vis_packet_size - 1] = orig_node->router->packet_count;

		}

	}

	if ( vis_packet_size == 6 ) {

		debugFree( vis_packet, 1107 );
		vis_packet = NULL;
		vis_packet_size = 0;

	}

}



void send_vis_packet() {

	generate_vis_packet();

	if ( vis_packet != NULL )
		send_udp_packet( vis_packet, vis_packet_size, &vis_if.addr, vis_if.sock );

}



int8_t batman() {

	struct list_head *if_pos, *neigh_pos, *hna_pos, *hna_pos_tmp, *forw_pos, *forw_pos_tmp;
	struct orig_node *orig_neigh_node, *orig_node;
	struct batman_if *batman_if, *if_incoming;
	struct neigh_node *neigh_node;
	struct hna_node *hna_node;
	struct forw_node *forw_node;
	uint32_t neigh, hna, netmask, debug_timeout, vis_timeout, select_timeout, curr_time;
	unsigned char in[2001], *hna_recv_buff;
	static char orig_str[ADDR_STR_LEN], neigh_str[ADDR_STR_LEN], ifaddr_str[ADDR_STR_LEN];
	int16_t hna_buff_count, hna_buff_len;
	uint8_t forward_old, if_rp_filter_all_old, if_rp_filter_default_old, if_send_redirects_all_old, if_send_redirects_default_old;
	uint8_t is_my_addr, is_my_orig, is_broadcast, is_duplicate, is_bidirectional, is_bntog, forward_duplicate_packet, has_unidirectional_flag, has_directlink_flag, has_version;
	int8_t res;


	debug_timeout = vis_timeout = get_time();

	if ( NULL == ( orig_hash = hash_new( 128, compare_orig, choose_orig ) ) )
		return(-1);

	/* for profiling the functions */
	prof_init( PROF_choose_gw, "choose_gw" );
	prof_init( PROF_update_routes, "update_routes" );
	prof_init( PROF_update_gw_list, "update_gw_list" );
	prof_init( PROF_is_duplicate, "isDuplicate" );
	prof_init( PROF_get_orig_node, "get_orig_node" );
	prof_init( PROF_update_originator, "update_orig" );
	prof_init( PROF_purge_originator, "purge_orig" );
	prof_init( PROF_schedule_forward_packet, "schedule_forward_packet" );
	prof_init( PROF_send_outstanding_packets, "send_outstanding_packets" );

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

		batman_if = list_entry( if_pos, struct batman_if, list );

		batman_if->out.ip.version = 4;
		batman_if->out.ip.ihl = 5;
		batman_if->out.ip.tos = 0;
		batman_if->out.ip.frag_off = 0;
		batman_if->out.ip.ttl = 64;
		batman_if->out.ip.protocol = IPPROTO_UDP;
		batman_if->out.ip.saddr = batman_if->addr.sin_addr.s_addr;
		batman_if->out.ip.daddr = batman_if->broad.sin_addr.s_addr;

		batman_if->out.udp.source = htons( PORT );
		batman_if->out.udp.dest = htons( PORT );
		batman_if->out.udp.len = htons( (u_short)( sizeof(struct orig_packet) - sizeof(struct iphdr) ) );
		batman_if->out.udp.check = 0;

		batman_if->out.bat_packet.orig = batman_if->addr.sin_addr.s_addr;
		batman_if->out.bat_packet.flags = 0x00;
		batman_if->out.bat_packet.ttl = TTL;
		batman_if->out.bat_packet.seqno = 1;
		batman_if->out.bat_packet.gwflags = gateway_class;
		batman_if->out.bat_packet.version = COMPAT_VERSION;

		batman_if->if_rp_filter_old = get_rp_filter( batman_if->dev );
		set_rp_filter( 0 , batman_if->dev );

		batman_if->if_send_redirects_old = get_send_redirects( batman_if->dev );
		set_send_redirects( 0 , batman_if->dev );

		schedule_own_packet( batman_if );

	}

	if_rp_filter_all_old = get_rp_filter( "all" );
	if_rp_filter_default_old = get_rp_filter( "default" );

	if_send_redirects_all_old = get_send_redirects( "all" );
	if_send_redirects_default_old = get_send_redirects( "default" );

	set_rp_filter( 0, "all" );
	set_rp_filter( 0, "default" );

	set_send_redirects( 0, "all" );
	set_send_redirects( 0, "default" );

	forward_old = get_forwarding();
	set_forwarding(1);

	while ( !is_aborted() ) {

		debug_output( 4, " \n \n" );

		/* harden select_timeout against sudden time change (e.g. ntpdate) */
		curr_time = get_time();
		select_timeout = ( curr_time < ((struct forw_node *)forw_list.next)->send_time ? ((struct forw_node *)forw_list.next)->send_time - curr_time : 10 );

		res = receive_packet( in + sizeof(struct iphdr) + sizeof(struct udphdr), sizeof(in) - sizeof(struct iphdr) - sizeof(struct udphdr), &hna_buff_len, &neigh, select_timeout, &if_incoming );

		if ( res < 0 )
			return -1;

		if ( res > 0 ) {

			curr_time = get_time();

			addr_to_string( ((struct orig_packet *)&in)->bat_packet.orig, orig_str, sizeof(orig_str) );
			addr_to_string( neigh, neigh_str, sizeof(neigh_str) );
			addr_to_string( if_incoming->addr.sin_addr.s_addr, ifaddr_str, sizeof(ifaddr_str) );

			is_my_addr = is_my_orig = is_broadcast = is_duplicate = is_bidirectional = is_bntog = forward_duplicate_packet = 0;

			has_unidirectional_flag = ((struct orig_packet *)&in)->bat_packet.flags & UNIDIRECTIONAL ? 1 : 0;
			has_directlink_flag = ((struct orig_packet *)&in)->bat_packet.flags & DIRECTLINK ? 1 : 0;
			has_version = ((struct orig_packet *)&in)->bat_packet.version;

			debug_output( 4, "Received BATMAN packet via NB: %s , IF: %s %s (from OG: %s, seqno %d, TTL %d, V %d, UDF %d, IDF %d) \n", neigh_str, if_incoming->dev, ifaddr_str, orig_str, ((struct orig_packet *)&in)->bat_packet.seqno, ((struct orig_packet *)&in)->bat_packet.ttl, has_version, has_unidirectional_flag, has_directlink_flag );

			hna_buff_len -= sizeof(struct bat_packet);
			hna_recv_buff = ( hna_buff_len > 4 ? in + sizeof(struct orig_packet) : NULL );

			list_for_each( if_pos, &if_list ) {

				batman_if = list_entry(if_pos, struct batman_if, list);

				if ( neigh == batman_if->addr.sin_addr.s_addr )
					is_my_addr = 1;

				if ( ((struct orig_packet *)&in)->bat_packet.orig == batman_if->addr.sin_addr.s_addr )
					is_my_orig = 1;

				if ( neigh == batman_if->broad.sin_addr.s_addr )
					is_broadcast = 1;

			}


			if ( ((struct orig_packet *)&in)->bat_packet.gwflags != 0 )
				debug_output( 4, "Is an internet gateway (class %i) \n", ((struct orig_packet *)&in)->bat_packet.gwflags );

			if ( hna_buff_len > 4 ) {

				debug_output( 4, "HNA information received (%i HNA network%s): \n", hna_buff_len / 5, ( hna_buff_len / 5 > 1 ? "s": "" ) );
				hna_buff_count = 0;

				while ( ( hna_buff_count + 1 ) * 5 <= hna_buff_len ) {

					memmove( &hna, ( uint32_t *)&hna_recv_buff[ hna_buff_count * 5 ], 4 );
					netmask = ( uint32_t )hna_recv_buff[ ( hna_buff_count * 5 ) + 4 ];

					addr_to_string( hna, orig_str, sizeof (orig_str) );

					if ( ( netmask > 0 ) && ( netmask < 33 ) )
						debug_output( 4, "hna: %s/%i\n", orig_str, netmask );
					else
						debug_output( 4, "hna: %s/%i -> ignoring (invalid netmask) \n", orig_str, netmask );

					hna_buff_count++;

				}

			}


			if ( ((struct orig_packet *)&in)->bat_packet.version != COMPAT_VERSION ) {

				debug_output( 4, "Drop packet: incompatible batman version (%i) \n", ((struct orig_packet *)&in)->bat_packet.version );

			} else if ( is_my_addr ) {

				debug_output( 4, "Drop packet: received my own broadcast (sender: %s) \n", neigh_str );

			} else if ( is_broadcast ) {

				debug_output( 4, "Drop packet: ignoring all packets with broadcast source IP (sender: %s) \n", neigh_str );

			} else if ( is_my_orig ) {

				orig_neigh_node = get_orig_node( neigh );

				debug_output( 4, "received my own OGM via NB lastTxIfSeqno: %d, currRxSeqno: %d, prevRxSeqno: %d, currRxSeqno-prevRxSeqno %d \n", ( if_incoming->out.bat_packet.seqno - 2 ), ((struct orig_packet *)&in)->bat_packet.seqno, orig_neigh_node->bidirect_link[if_incoming->if_num], ((struct orig_packet *)&in)->bat_packet.seqno - orig_neigh_node->bidirect_link[if_incoming->if_num] );

				/* neighbour has to indicate direct link and it has to come via the corresponding interface */
				/* if received seqno equals last send seqno save new seqno for bidirectional check */
				if ( ( ((struct orig_packet *)&in)->bat_packet.flags & DIRECTLINK ) && ( if_incoming->addr.sin_addr.s_addr == ((struct orig_packet *)&in)->bat_packet.orig ) && ( ((struct orig_packet *)&in)->bat_packet.seqno - if_incoming->out.bat_packet.seqno + 2 == 0 ) ) {

					orig_neigh_node->bidirect_link[if_incoming->if_num] = ((struct orig_packet *)&in)->bat_packet.seqno;

					debug_output( 4, "indicating bidirectional link - updating bidirect_link seqno \n");

				} else {

					debug_output( 4, "NOT indicating bidirectional link - NOT updating bidirect_link seqno \n");

				}

				debug_output( 4, "Drop packet: originator packet from myself (via neighbour) \n" );

			} else if ( ((struct orig_packet *)&in)->bat_packet.flags & UNIDIRECTIONAL ) {

				debug_output( 4, "Drop packet: originator packet with unidirectional flag \n" );

			} else {

				orig_node = get_orig_node( ((struct orig_packet *)&in)->bat_packet.orig );

				/* if sender is a direct neighbor the sender ip equals originator ip */
				orig_neigh_node = ( ((struct orig_packet *)&in)->bat_packet.orig == neigh ? orig_node : get_orig_node( neigh ) );

				/* drop packet if sender is not a direct neighbor and if we no route towards it */
				if ( ( ((struct orig_packet *)&in)->bat_packet.orig != neigh ) && ( orig_neigh_node->router == NULL ) ) {

					debug_output( 4, "Drop packet: OGM via unkown neighbor! \n" );

				} else {

					is_duplicate = isDuplicate( orig_node, ((struct orig_packet *)&in)->bat_packet.seqno );
					is_bidirectional = isBidirectionalNeigh( orig_neigh_node, if_incoming );

					/* update ranking */
					if ( ( is_bidirectional ) && ( !is_duplicate ) )
						update_orig( orig_node, (struct bat_packet *)(in + sizeof(struct iphdr) + sizeof(struct udphdr)), neigh, if_incoming, hna_recv_buff, hna_buff_len, curr_time );

					is_bntog = isBntog( neigh, orig_node );

					/* is single hop (direct) neighbour */
					if ( ((struct orig_packet *)&in)->bat_packet.orig == neigh ) {

						/* it is our best route towards him */
						if ( is_bidirectional && is_bntog ) {

							/* mark direct link on incoming interface */
							schedule_forward_packet( (struct orig_packet *)&in, 0, 1, hna_recv_buff, hna_buff_len, if_incoming );

							debug_output( 4, "Forward packet: rebroadcast neighbour packet with direct link flag \n" );

						/* if an unidirectional neighbour sends us a packet - retransmit it with unidirectional flag to tell him that we get its packets */
						/* if a bidirectional neighbour sends us a packet - retransmit it with unidirectional flag if it is not our best link to it in order to prevent routing problems */
						} else if ( ( is_bidirectional && !is_bntog ) || ( !is_bidirectional ) ) {

							schedule_forward_packet( (struct orig_packet *)&in, 1, 1, hna_recv_buff, hna_buff_len, if_incoming );

							debug_output( 4, "Forward packet: rebroadcast neighbour packet with direct link and unidirectional flag \n" );

						}

					/* multihop originator */
					} else {

						if ( is_bidirectional && is_bntog ) {

							if ( !is_duplicate ) {

								schedule_forward_packet( (struct orig_packet *)&in, 0, 0, hna_recv_buff, hna_buff_len, if_incoming );

								debug_output( 4, "Forward packet: rebroadcast originator packet \n" );

							} else { /* is_bntog anyway */

								list_for_each( neigh_pos, &orig_node->neigh_list ) {

									neigh_node = list_entry(neigh_pos, struct neigh_node, list);

									if ( ( neigh_node->addr == neigh ) && ( neigh_node->if_incoming == if_incoming ) ) {

										if ( neigh_node->last_ttl == ((struct orig_packet *)&in)->bat_packet.ttl ) {

											forward_duplicate_packet = 1;

											/* also update only last_valid time if arrived (and rebroadcasted because best neighbor) */
											orig_node->last_valid = curr_time;
											neigh_node->last_valid = curr_time;

										}

										break;

									}

								}

								/* we are forwarding duplicate o-packets if they come via our best neighbour and ttl is valid */
								if ( forward_duplicate_packet ) {

									schedule_forward_packet( (struct orig_packet *)&in, 0, 0, hna_recv_buff, hna_buff_len, if_incoming );

									debug_output( 4, "Forward packet: duplicate packet received via best neighbour with best ttl \n" );

								} else {

									debug_output( 4, "Drop packet: duplicate packet received via best neighbour but not best ttl \n" );

								}

							}

						} else {

							debug_output( 4, "Drop packet: received via bidirectional link: %s, BNTOG: %s !\n", ( is_bidirectional ? "YES" : "NO" ), ( is_bntog ? "YES" : "NO" ) );

						}

					}

				}

			}

		}


		send_outstanding_packets();


		if ( debug_timeout + 1000 < curr_time ) {

			debug_timeout = curr_time;

			purge_orig( curr_time );

			debug_orig();

			checkIntegrity();

			if ( debug_clients.clients_num[2] > 0 )
				prof_print();

			if ( ( routing_class != 0 ) && ( curr_gateway == NULL ) )
				choose_gw();

			if ( ( vis_if.sock ) && ( vis_timeout + 10000 < curr_time ) ) {

				vis_timeout = curr_time;
				send_vis_packet();

			}

		}

	}


	if ( debug_level > 0 )
		printf( "Deleting all BATMAN routes\n" );

	purge_orig( get_time() + ( 5 * PURGE_TIMEOUT ) + originator_interval );

	hash_destroy( orig_hash );


	list_for_each_safe( hna_pos, hna_pos_tmp, &hna_list ) {

		hna_node = list_entry(hna_pos, struct hna_node, list);

		debugFree( hna_node, 1103 );

	}

	if ( hna_buff != NULL )
		debugFree( hna_buff, 1104 );


	list_for_each_safe( forw_pos, forw_pos_tmp, &forw_list ) {

		forw_node = list_entry( forw_pos, struct forw_node, list );

		list_del( (struct list_head *)&forw_list, forw_pos, &forw_list );

		debugFree( forw_node->pack_buff, 1105 );
		debugFree( forw_node, 1106 );

	}

	if ( vis_packet != NULL )
		debugFree( vis_packet, 1108 );

	set_forwarding( forward_old );

	list_for_each( if_pos, &if_list ) {

		batman_if = list_entry(if_pos, struct batman_if, list);

		set_rp_filter( batman_if->if_rp_filter_old , batman_if->dev );
		set_send_redirects( batman_if->if_send_redirects_old , batman_if->dev );

	}

	set_rp_filter( if_rp_filter_all_old, "all" );
	set_rp_filter( if_rp_filter_default_old, "default" );

	set_send_redirects( if_send_redirects_all_old, "all" );
	set_send_redirects( if_send_redirects_default_old, "default" );

	return 0;

}
