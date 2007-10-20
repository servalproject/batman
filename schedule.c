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
#include "schedule.h"



void schedule_own_packet( struct batman_if *batman_if ) {

	struct forw_node *forw_node_new, *forw_packet_tmp = NULL;
	struct list_head *list_pos, *prev_list_head;
	struct hash_it_t *hashit = NULL;
	struct orig_node *orig_node;


	forw_node_new = debugMalloc( sizeof(struct forw_node), 501 );

	INIT_LIST_HEAD( &forw_node_new->list );

	forw_node_new->send_time = get_time() + originator_interval - JITTER + rand_num( 2 * JITTER );
	forw_node_new->if_outgoing = batman_if;
	forw_node_new->own = 1;

	/* non-primary interfaces do not send hna information */
	if ( ( num_hna > 0 ) && ( batman_if->if_num == 0 ) ) {

		forw_node_new->pack_buff = debugMalloc( sizeof(struct bat_packet) + num_hna * 5 * sizeof(unsigned char), 502 );
		memcpy( forw_node_new->pack_buff, (unsigned char *)&batman_if->out, sizeof(struct bat_packet) );
		memcpy( forw_node_new->pack_buff + sizeof(struct bat_packet), hna_buff, num_hna * 5 * sizeof(unsigned char) );
		forw_node_new->pack_buff_len = sizeof(struct bat_packet) + num_hna * 5 * sizeof(unsigned char);

	} else {

		forw_node_new->pack_buff = debugMalloc( sizeof(struct bat_packet), 503 );
		memcpy( forw_node_new->pack_buff, &batman_if->out, sizeof(struct bat_packet) );
		forw_node_new->pack_buff_len = sizeof(struct bat_packet);

	}

	prev_list_head = (struct list_head *)&forw_list;

	list_for_each( list_pos, &forw_list ) {

		forw_packet_tmp = list_entry( list_pos, struct forw_node, list );

		if ( forw_packet_tmp->send_time > forw_node_new->send_time ) {

			list_add_before( prev_list_head, list_pos, &forw_node_new->list );
			break;

		}

		prev_list_head = &forw_packet_tmp->list;

	}

	if ( ( forw_packet_tmp == NULL ) || ( forw_packet_tmp->send_time <= forw_node_new->send_time ) )
		list_add_tail( &forw_node_new->list, &forw_list );

	batman_if->out.seqno++;


	while ( NULL != ( hashit = hash_iterate( orig_hash, hashit ) ) ) {

		orig_node = hashit->bucket->data;

		debug_output( 4, "count own bcast (schedule_own_packet): old = %i, ", orig_node->bcast_own_sum[batman_if->if_num] );
		bit_get_packet( (TYPE_OF_WORD *)&(orig_node->bcast_own[batman_if->if_num * NUM_WORDS]), 1, 0 );
		orig_node->bcast_own_sum[batman_if->if_num] = bit_packet_count( (TYPE_OF_WORD *)&(orig_node->bcast_own[batman_if->if_num * NUM_WORDS]) );
		debug_output( 4, "new = %i \n", orig_node->bcast_own_sum[batman_if->if_num] );

	}

}



void schedule_forward_packet(struct orig_node *orig_node, struct bat_packet *in, uint32_t neigh, uint8_t unidirectional, uint8_t directlink, unsigned char *hna_recv_buff, int16_t hna_buff_len, struct batman_if *if_outgoing) {

	prof_start( PROF_schedule_forward_packet );
	struct forw_node *forw_node_new;
	uint8_t tq_avg = 0;

	debug_output( 4, "schedule_forward_packet():  \n" );

	if ( in->ttl <= 1 ) {

		debug_output( 4, "ttl exceeded \n" );

	} else {

		forw_node_new = debugMalloc( sizeof(struct forw_node), 504 );

		INIT_LIST_HEAD( &forw_node_new->list );

		if ( ( hna_buff_len > 0 ) && ( hna_recv_buff != NULL ) ) {

			forw_node_new->pack_buff = debugMalloc( sizeof(struct bat_packet) + hna_buff_len, 505 );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct bat_packet) );
			memcpy( forw_node_new->pack_buff + sizeof(struct bat_packet), hna_recv_buff, hna_buff_len );
			forw_node_new->pack_buff_len = sizeof(struct bat_packet) + hna_buff_len;

		} else {

			forw_node_new->pack_buff = debugMalloc( sizeof(struct bat_packet), 506 );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct bat_packet) );
			forw_node_new->pack_buff_len = sizeof(struct bat_packet);

		}


		((struct bat_packet *)forw_node_new->pack_buff)->ttl--;
		((struct bat_packet *)forw_node_new->pack_buff)->old_orig = neigh;

		/* rebroadcast tq of our best ranking neighbor to ensure the rebroadcast of our best tq value */
		if ((orig_node->router != NULL) && (orig_node->router->tq_avg != 0)) {

			/* rebroadcast ogm of best ranking neighbor as is */
			if (orig_node->router->addr != neigh) {

				((struct bat_packet *)forw_node_new->pack_buff)->tq = orig_node->router->tq_avg;
				((struct bat_packet *)forw_node_new->pack_buff)->ttl = orig_node->router->last_ttl - 1;

			}

			tq_avg = orig_node->router->tq_avg;

			if ((orig_node->router->orig_node->tq_own > TQ_MAX_VALUE - PERFECT_TQ_PENALTY) && (orig_node->router->orig_node->tq_asym_penality > TQ_MAX_VALUE - PERFECT_TQ_PENALTY))
				((struct bat_packet *)forw_node_new->pack_buff)->tq -= PERFECT_TQ_PENALTY;

		}

		debug_output( 4, "forwarding: tq_orig: %i, tq_avg: %i, tq_forw: %i, ttl_orig: %i, ttl_forw: %i \n", in->tq, tq_avg, ((struct bat_packet *)forw_node_new->pack_buff)->tq, in->ttl - 1, ((struct bat_packet *)forw_node_new->pack_buff)->ttl );

		forw_node_new->send_time = get_time();
		forw_node_new->own = 0;

		forw_node_new->if_outgoing = if_outgoing;

		if ( unidirectional ) {

			((struct bat_packet *)forw_node_new->pack_buff)->flags = ( UNIDIRECTIONAL | DIRECTLINK );

		} else if ( directlink ) {

			((struct bat_packet *)forw_node_new->pack_buff)->flags = DIRECTLINK;

		} else {

			((struct bat_packet *)forw_node_new->pack_buff)->flags = 0x00;

		}

		list_add( &forw_node_new->list, &forw_list );

	}

	prof_stop( PROF_schedule_forward_packet );

}



void send_outstanding_packets() {

	prof_start( PROF_send_outstanding_packets );
	struct forw_node *forw_node;
	struct list_head *forw_pos, *if_pos, *temp;
	struct batman_if *batman_if;
	static char orig_str[ADDR_STR_LEN];
	uint8_t directlink;
	uint32_t curr_time;


	if ( list_empty( &forw_list ) )
		return;

	curr_time = get_time();

	list_for_each_safe( forw_pos, temp, &forw_list ) {

		forw_node = list_entry( forw_pos, struct forw_node, list );

		if ( forw_node->send_time <= curr_time ) {

			addr_to_string( ((struct bat_packet *)forw_node->pack_buff)->orig, orig_str, ADDR_STR_LEN );

			directlink = ( ( ((struct bat_packet *)forw_node->pack_buff)->flags & DIRECTLINK ) ? 1 : 0 );

			/* change sequence number to network order */
			((struct bat_packet *)forw_node->pack_buff)->seqno = htons( ((struct bat_packet *)forw_node->pack_buff)->seqno );


			if ( ((struct bat_packet *)forw_node->pack_buff)->flags & UNIDIRECTIONAL ) {

				if ( forw_node->if_outgoing != NULL ) {

					debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ntohs( ((struct bat_packet *)forw_node->pack_buff)->seqno ), ((struct bat_packet *)forw_node->pack_buff)->ttl, forw_node->if_outgoing->dev );

					if ( send_udp_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 )
						restore_and_exit(0);

				} else {

					debug_output( 0, "Error - can't forward packet with UDF: outgoing iface not specified \n" );

				}

			/* multihomed peer assumed */
			} else if ( ( directlink ) && ( ((struct bat_packet *)forw_node->pack_buff)->ttl == 1 ) ) {

				if ( ( forw_node->if_outgoing != NULL ) ) {

					if ( send_udp_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 )
						restore_and_exit(0);

				} else {

					debug_output( 0, "Error - can't forward packet with IDF: outgoing iface not specified (multihomed) \n" );

				}

			} else {

				if ( ( directlink ) && ( forw_node->if_outgoing == NULL ) ) {

					debug_output( 0, "Error - can't forward packet with IDF: outgoing iface not specified \n" );

				} else {

					/* non-primary interfaces are only broadcasted on their interface */
					if ( ( forw_node->own ) && ( forw_node->if_outgoing->if_num > 0 ) ) {

						debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ntohs( ((struct bat_packet *)forw_node->pack_buff)->seqno ), ((struct bat_packet *)forw_node->pack_buff)->ttl, forw_node->if_outgoing->dev );

						if ( send_udp_packet( forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock ) < 0 )
							restore_and_exit(0);

					} else {

						list_for_each(if_pos, &if_list) {

							batman_if = list_entry(if_pos, struct batman_if, list);

							if ( ( directlink ) && ( forw_node->if_outgoing == batman_if ) ) {
								((struct bat_packet *)forw_node->pack_buff)->flags = DIRECTLINK;
							} else {
								((struct bat_packet *)forw_node->pack_buff)->flags = 0x00;
							}

							debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ntohs( ((struct bat_packet *)forw_node->pack_buff)->seqno ), ((struct bat_packet *)forw_node->pack_buff)->ttl, batman_if->dev );

							if ( send_udp_packet( forw_node->pack_buff, forw_node->pack_buff_len, &batman_if->broad, batman_if->udp_send_sock ) < 0 )
								restore_and_exit(0);

						}

					}

				}

			}

			list_del( (struct list_head *)&forw_list, forw_pos, &forw_list );

			if ( forw_node->own )
				schedule_own_packet( forw_node->if_outgoing );

			debugFree( forw_node->pack_buff, 1501 );
			debugFree( forw_node, 1502 );

		} else {

			break;

		}

	}

	prof_stop( PROF_send_outstanding_packets );

}


