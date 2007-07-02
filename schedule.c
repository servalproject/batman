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



void schedule_own_packet( struct batman_if *batman_if ) {

	struct forw_node *forw_node_new, *forw_packet_tmp = NULL;
	struct list_head *list_pos, *prev_list_head;


	forw_node_new = debugMalloc( sizeof(struct forw_node), 501 );

	INIT_LIST_HEAD( &forw_node_new->list );

	forw_node_new->send_time = get_time() + originator_interval - JITTER + rand_num( 2 * JITTER );
	forw_node_new->if_outgoing = batman_if;
	forw_node_new->own = 1;

	if ( num_hna > 0 ) {

		forw_node_new->pack_buff = debugMalloc( sizeof(struct orig_packet) + num_hna * 5 * sizeof(unsigned char), 502 );
		memcpy( forw_node_new->pack_buff, (unsigned char *)&batman_if->out, sizeof(struct orig_packet) );
		memcpy( forw_node_new->pack_buff + sizeof(struct orig_packet), hna_buff, num_hna * 5 * sizeof(unsigned char) );
		forw_node_new->pack_buff_len = sizeof(struct orig_packet) + num_hna * 5 * sizeof(unsigned char);

	} else {

		forw_node_new->pack_buff = debugMalloc( sizeof(struct orig_packet), 503 );
		memcpy( forw_node_new->pack_buff, &batman_if->out, sizeof(struct orig_packet) );
		forw_node_new->pack_buff_len = sizeof(struct orig_packet);

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

	batman_if->out.bat_packet.seqno++;

}



void schedule_forward_packet( struct orig_packet *in, uint8_t unidirectional, uint8_t directlink, unsigned char *hna_recv_buff, int16_t hna_buff_len, struct batman_if *if_outgoing ) {

	prof_start( PROF_schedule_forward_packet );
	struct forw_node *forw_node_new;

	debug_output( 4, "schedule_forward_packet():  \n" );

	if ( in->bat_packet.ttl <= 1 ) {

		debug_output( 4, "ttl exceeded \n" );

	} else {

		forw_node_new = debugMalloc( sizeof(struct forw_node), 504 );

		INIT_LIST_HEAD( &forw_node_new->list );

		if ( hna_buff_len > 0 ) {

			forw_node_new->pack_buff = debugMalloc( sizeof(struct orig_packet) + hna_buff_len, 505 );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct orig_packet) );
			memcpy( forw_node_new->pack_buff + sizeof(struct orig_packet), hna_recv_buff, hna_buff_len );
			forw_node_new->pack_buff_len = sizeof(struct orig_packet) + hna_buff_len;

		} else {

			forw_node_new->pack_buff = debugMalloc( sizeof(struct orig_packet), 506 );
			memcpy( forw_node_new->pack_buff, in, sizeof(struct orig_packet) );
			forw_node_new->pack_buff_len = sizeof(struct orig_packet);

		}


		((struct orig_packet *)forw_node_new->pack_buff)->bat_packet.ttl--;
		forw_node_new->send_time = get_time();
		forw_node_new->own = 0;

		forw_node_new->if_outgoing = if_outgoing;

		if ( unidirectional ) {

			((struct orig_packet *)forw_node_new->pack_buff)->bat_packet.flags = ( UNIDIRECTIONAL | DIRECTLINK );

		} else if ( directlink ) {

			((struct orig_packet *)forw_node_new->pack_buff)->bat_packet.flags = DIRECTLINK;

		} else {

			((struct orig_packet *)forw_node_new->pack_buff)->bat_packet.flags = 0x00;

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

			addr_to_string( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.orig, orig_str, ADDR_STR_LEN );

			directlink = ( ( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.flags & DIRECTLINK ) ? 1 : 0 );

			/* change sequence number to network order */
			((struct orig_packet *)forw_node->pack_buff)->bat_packet.seqno = htons( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.seqno );


			if ( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.flags & UNIDIRECTIONAL ) {

				if ( forw_node->if_outgoing != NULL ) {

					debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ntohs( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.seqno ), ((struct orig_packet *)forw_node->pack_buff)->bat_packet.ttl, forw_node->if_outgoing->dev );

					if ( send_raw_packet( forw_node->pack_buff, forw_node->pack_buff_len, forw_node->if_outgoing ) < 0 )
						restore_and_exit(0);

				} else {

					debug_output( 0, "Error - can't forward packet with UDF: outgoing iface not specified \n" );

				}

			/* multihomed peer assumed */
			} else if ( ( directlink ) && ( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.ttl == 1 ) ) {

				if ( ( forw_node->if_outgoing != NULL ) ) {

					if ( send_raw_packet( forw_node->pack_buff, forw_node->pack_buff_len, forw_node->if_outgoing ) < 0 )
						restore_and_exit(0);

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
							((struct orig_packet *)forw_node->pack_buff)->bat_packet.flags = DIRECTLINK;
						} else {
							((struct orig_packet *)forw_node->pack_buff)->bat_packet.flags = 0x00;
						}

						debug_output( 4, "Forwarding packet (originator %s, seqno %d, TTL %d) on interface %s\n", orig_str, ntohs( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.seqno ), ((struct orig_packet *)forw_node->pack_buff)->bat_packet.ttl, batman_if->dev );

						/* non-primary interfaces do not send hna information */
						if ( ( forw_node->own ) && ( ((struct orig_packet *)forw_node->pack_buff)->bat_packet.orig != ((struct batman_if *)if_list.next)->addr.sin_addr.s_addr ) ) {

							if ( send_raw_packet( forw_node->pack_buff, sizeof(struct orig_packet), batman_if ) < 0 )
								restore_and_exit(0);

						} else {

							if ( send_raw_packet( forw_node->pack_buff, forw_node->pack_buff_len, batman_if ) < 0 )
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


