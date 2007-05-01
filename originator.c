/* Copyright (C) 2006 B.A.T.M.A.N. contributors:
 * Simon Wunderlich, Marek Lindner
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



#include <string.h>
#include <stdlib.h>
#include "os.h"
#include "batman.h"



/* needed for hash, compares 2 struct orig_node, but only their ip-addresses. assumes that
 * the ip address is the first field in the struct */
int compare_orig( void *data1, void *data2 ) {

	return ( memcmp( data1, data2, 4 ) );

}



/* hashfunction to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
int choose_orig( void *data, int32_t size ) {

	unsigned char *key= data;
	uint32_t hash = 0;
	size_t i;

	for (i = 0; i < 4; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return (hash%size);

}



/* this function finds or creates an originator entry for the given address if it does not exits */
struct orig_node *get_orig_node( uint32_t addr ) {

	prof_start( PROF_get_orig_node );
	struct orig_node *orig_node;
	struct hashtable_t *swaphash;
	static char orig_str[ADDR_STR_LEN];


	orig_node = ((struct orig_node *)hash_find( orig_hash, &addr ));

	if ( orig_node != NULL ) {

		prof_stop( PROF_get_orig_node );
		return orig_node;

	}


	addr_to_string( addr, orig_str, ADDR_STR_LEN );
	debug_output( 4, "Creating new originator: %s \n", orig_str );

	orig_node = debugMalloc( sizeof(struct orig_node), 401 );
	memset(orig_node, 0, sizeof(struct orig_node));
	INIT_LIST_HEAD(&orig_node->neigh_list);

	orig_node->orig = addr;
	orig_node->router = NULL;
	orig_node->batman_if = NULL;

	orig_node->bidirect_link = debugMalloc( found_ifs * sizeof(uint16_t), 402 );
	memset( orig_node->bidirect_link, 0, found_ifs * sizeof(uint16_t) );

	hash_add( orig_hash, orig_node );

	if ( orig_hash->elements * 4 > orig_hash->size ) {

		swaphash = hash_resize( orig_hash, orig_hash->size * 2 );

		if ( swaphash == NULL ) {

			debug_output( 0, "Couldn't resize hash table \n" );
			restore_and_exit();

		}

		orig_hash = swaphash;

	}

	prof_stop( PROF_get_orig_node );
	return orig_node;

}



void update_orig( struct orig_node *orig_node, struct packet *in, uint32_t neigh, struct batman_if *if_incoming, unsigned char *hna_recv_buff, int16_t hna_buff_len ) {

	prof_start( PROF_update_originator );
	struct list_head *neigh_pos;
	struct neigh_node *neigh_node = NULL, *tmp_neigh_node, *best_neigh_node;
	uint8_t max_packet_count = 0, is_new_seqno = 0;


	debug_output( 4, "update_originator(): Searching and updating originator entry of received packet,  \n" );


	list_for_each( neigh_pos, &orig_node->neigh_list ) {

		tmp_neigh_node = list_entry( neigh_pos, struct neigh_node, list );

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

		neigh_node = debugMalloc( sizeof (struct neigh_node), 403 );
		memset( neigh_node, 0, sizeof(struct neigh_node) );
		INIT_LIST_HEAD( &neigh_node->list );

		neigh_node->addr = neigh;
		neigh_node->if_incoming = if_incoming;

		list_add_tail( &neigh_node->list, &orig_node->neigh_list );

	} else {

		debug_output( 4, "Updating existing last-hop neighbour of originator\n" );

	}


	is_new_seqno = bit_get_packet( neigh_node->seq_bits, in->seqno - orig_node->last_seqno, 1 );
	neigh_node->packet_count = bit_packet_count( neigh_node->seq_bits );

	if ( neigh_node->packet_count > max_packet_count ) {

		max_packet_count = neigh_node->packet_count;
		best_neigh_node = neigh_node;

	}


	neigh_node->last_aware = get_time();

	if ( is_new_seqno ) {

		debug_output( 4, "updating last_seqno: old %d, new %d \n", orig_node->last_seqno, in->seqno  );

		orig_node->last_seqno = in->seqno;
		neigh_node->last_ttl = in->ttl;

	}

	/* update routing table and check for changed hna announcements */
	update_routes( orig_node, best_neigh_node, hna_recv_buff, hna_buff_len );

	if ( orig_node->gwflags != in->gwflags )
		update_gw_list( orig_node, in->gwflags );

	orig_node->gwflags = in->gwflags;

	prof_stop( PROF_update_originator );

}



void purge_orig( uint32_t curr_time ) {

	prof_start( PROF_purge_orginator );
	struct hash_it_t *hashit = NULL;
	struct list_head *neigh_pos, *neigh_temp;
	struct list_head *gw_pos, *gw_pos_tmp;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node, *best_neigh_node;
	struct gw_node *gw_node;
	uint8_t gw_purged = 0, neigh_purged;
	static char orig_str[ADDR_STR_LEN];


	/* for all origins... */
	while ( NULL != ( hashit = hash_iterate( orig_hash, hashit ) ) ) {

		orig_node = hashit->bucket->data;

		if ( (int)( ( orig_node->last_aware + ( 2 * TIMEOUT ) ) < curr_time ) ) {

			addr_to_string( orig_node->orig, orig_str, ADDR_STR_LEN );
			debug_output( 4, "Orginator timeout: originator %s, last_aware %u) \n", orig_str, orig_node->last_aware );

			hash_remove_bucket( orig_hash, hashit );

			/* for all neighbours towards this orginator ... */
			list_for_each_safe( neigh_pos, neigh_temp, &orig_node->neigh_list ) {
				neigh_node = list_entry(neigh_pos, struct neigh_node, list);

				list_del( neigh_pos );
				debugFree( neigh_node, 1401 );

			}

			list_for_each( gw_pos, &gw_list ) {

				gw_node = list_entry( gw_pos, struct gw_node, list );

				if ( gw_node->deleted )
					continue;

				if ( gw_node->orig_node == orig_node ) {

					addr_to_string( gw_node->orig_node->orig, orig_str, ADDR_STR_LEN );
					debug_output( 3, "Removing gateway %s from gateway list \n", orig_str );

					gw_node->deleted = get_time();

					gw_purged = 1;

					break;

				}

			}

			update_routes( orig_node, NULL, NULL, 0 );

			debugFree( orig_node->bidirect_link, 1402 );
			debugFree( orig_node, 1403 );

		} else {

			best_neigh_node = NULL;
			neigh_purged = 0;

			/* for all neighbours towards this orginator ... */
			list_for_each_safe( neigh_pos, neigh_temp, &orig_node->neigh_list ) {

				neigh_node = list_entry( neigh_pos, struct neigh_node, list );

				if ( (int)( ( neigh_node->last_aware + ( 2 * TIMEOUT ) ) < curr_time ) ) {

					neigh_purged = 1;
					list_del( neigh_pos );
					debugFree( neigh_node, 1404 );

				} else {

					if ( ( best_neigh_node == NULL ) || ( neigh_node->packet_count > best_neigh_node->packet_count ) )
						best_neigh_node = neigh_node;


				}

			}

			if ( ( neigh_purged ) && ( ( best_neigh_node == NULL ) || ( orig_node->router == NULL ) || ( best_neigh_node->packet_count > orig_node->router->packet_count ) ) )
				update_routes( orig_node, best_neigh_node, orig_node->hna_buff, orig_node->hna_buff_len );

		}

	}

	list_for_each_safe(gw_pos, gw_pos_tmp, &gw_list) {

		gw_node = list_entry(gw_pos, struct gw_node, list);

		if ( ( gw_node->deleted ) && ( (int)((gw_node->deleted + 3 * TIMEOUT) < curr_time) ) ) {

			list_del( gw_pos );
			debugFree( gw_pos, 1405 );

		}

	}

	prof_stop( PROF_purge_orginator );

	if ( gw_purged )
		choose_gw();

}



void debug_orig() {

	struct hash_it_t *hashit = NULL;
	struct list_head *forw_pos, *orig_pos, *neigh_pos;
	struct forw_node *forw_node;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	struct gw_node *gw_node;
	uint16_t batman_count = 0;
	static char str[ADDR_STR_LEN], str2[ADDR_STR_LEN];


	if ( debug_clients.clients_num[1] > 0 ) {

		debug_output( 2, "BOD\n" );

		if ( list_empty( &gw_list ) ) {

			debug_output( 2, "No gateways in range ... \n" );

		} else {

			debug_output( 2, "%''12s       %''15s (%''3s %''2s) \n", "Gateway", "Router", "%", "#" );

			list_for_each( orig_pos, &gw_list ) {

				gw_node = list_entry( orig_pos, struct gw_node, list );

				if ( gw_node->deleted )
					continue;

				addr_to_string( gw_node->orig_node->orig, str, sizeof (str) );
				addr_to_string( gw_node->orig_node->router->addr, str2, sizeof (str2) );

				if ( curr_gateway == gw_node ) {
					debug_output( 2, "=> %-15s %''15s (%''3i %''2i), gw_class %2i - %s, reliability: %i \n", str, str2, ( 100 * gw_node->orig_node->router->packet_count / SEQ_RANGE ), gw_node->orig_node->router->packet_count, gw_node->orig_node->gwflags, gw2string[gw_node->orig_node->gwflags], gw_node->unavail_factor );
				} else {
					debug_output( 2, "   %-15s %''15s (%''3i %''2i), gw_class %2i - %s, reliability: %i \n", str, str2, ( 100 * gw_node->orig_node->router->packet_count / SEQ_RANGE ), gw_node->orig_node->router->packet_count, gw_node->orig_node->gwflags, gw2string[gw_node->orig_node->gwflags], gw_node->unavail_factor );
				}

				batman_count++;

			}

			if ( batman_count == 0 )
				debug_output( 2, "No gateways in range ... \n" );

		}

		debug_output( 2, "EOD\n" );

	}

	if ( ( debug_clients.clients_num[0] > 0 ) || ( debug_clients.clients_num[3] > 0 ) ) {

		debug_output( 1, "BOD\n" );
		debug_output( 1, "%''15s %''15s (%''3s %''2s): %''20s\n", "Orginator", "Router", "%", "#", "potential gateways" );

		if ( debug_clients.clients_num[3] > 0 ) {

			debug_output( 4, "------------------ DEBUG ------------------ \n" );
			debug_output( 4, "Forward list \n" );

			list_for_each( forw_pos, &forw_list ) {
				forw_node = list_entry( forw_pos, struct forw_node, list );
				addr_to_string( ((struct packet *)forw_node->pack_buff)->orig, str, sizeof (str) );
				debug_output( 4, "    %s at %u \n", str, forw_node->send_time );
			}

			debug_output( 4, "Originator list \n" );
			debug_output( 4, "%''15s %''15s (%''3s %''2s): %''20s\n", "Orginator", "Gateway", "%", "#", "potential gateways" );

		}

		while ( NULL != ( hashit = hash_iterate( orig_hash, hashit ) ) ) {

			orig_node = hashit->bucket->data;

			if ( orig_node->router == NULL )
				continue;

			batman_count++;

			addr_to_string( orig_node->orig, str, sizeof (str) );
			addr_to_string( orig_node->router->addr, str2, sizeof (str2) );

			debug_output( 1, "%-15s %''15s (%3i %2i):", str, str2, ( 100 * orig_node->router->packet_count / SEQ_RANGE ), orig_node->router->packet_count );
			debug_output( 4, "%''15s %''15s (%3i %2i), last_aware:%u: \n", str, str2, ( 100 * orig_node->router->packet_count / SEQ_RANGE ), orig_node->router->packet_count, orig_node->last_aware );

			list_for_each( neigh_pos, &orig_node->neigh_list ) {
				neigh_node = list_entry( neigh_pos, struct neigh_node, list );

				addr_to_string( neigh_node->addr, str, sizeof (str) );

				debug_output( 1, " %''15s (%3i %2i)", str, ( 100 * neigh_node->packet_count / SEQ_RANGE ), neigh_node->packet_count );
				debug_output( 4, "\t\t%''15s (%3i %2i) \n", str, ( 100 * neigh_node->packet_count / SEQ_RANGE ), neigh_node->packet_count );

			}

			debug_output( 1, " \n" );

		}

		if ( batman_count == 0 ) {

			debug_output( 1, "No batman nodes in range ... \n" );
			debug_output( 4, "No batman nodes in range ... \n" );

		}

		debug_output( 1, "EOD\n" );
		debug_output( 4, "---------------------------------------------- END DEBUG \n" );

	}

}


