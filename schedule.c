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


	forw_node_new = debugMalloc(sizeof(struct forw_node), 501);

	INIT_LIST_HEAD(&forw_node_new->list);

	forw_node_new->send_time = get_time_msec() + originator_interval - JITTER + rand_num(2 * JITTER);
	forw_node_new->if_outgoing = batman_if;
	forw_node_new->own = 1;
	forw_node_new->num_packets = 0;
	forw_node_new->direct_link_flags = 0;

	/* non-primary interfaces do not send hna information */
	if ((num_hna > 0 ) && (batman_if->if_num == 0)) {

		forw_node_new->pack_buff = debugMalloc(MAX_AGGREGATION_BYTES, 502);
		memcpy(forw_node_new->pack_buff, (unsigned char *)&batman_if->out, sizeof(struct bat_packet));
		memcpy(forw_node_new->pack_buff + sizeof(struct bat_packet), hna_buff, num_hna * 5);
		forw_node_new->pack_buff_len = sizeof(struct bat_packet) + num_hna * 5;
		((struct bat_packet *)forw_node_new->pack_buff)->hna_len = num_hna;

	} else {

		forw_node_new->pack_buff = debugMalloc(MAX_AGGREGATION_BYTES, 503);
		memcpy(forw_node_new->pack_buff, &batman_if->out, sizeof(struct bat_packet));
		forw_node_new->pack_buff_len = sizeof(struct bat_packet);
		((struct bat_packet *)forw_node_new->pack_buff)->hna_len = 0;

	}

	/* change sequence number to network order */
	((struct bat_packet *)forw_node_new->pack_buff)->seqno = htons(((struct bat_packet *)forw_node_new->pack_buff)->seqno);

	prev_list_head = (struct list_head *)&forw_list;

	list_for_each(list_pos, &forw_list) {

		forw_packet_tmp = list_entry(list_pos, struct forw_node, list);

		if ((int)(forw_packet_tmp->send_time - forw_node_new->send_time) >= 0) {

			list_add_before(prev_list_head, list_pos, &forw_node_new->list);
			break;

		}

		prev_list_head = &forw_packet_tmp->list;
		forw_packet_tmp = NULL;

	}

	if (forw_packet_tmp == NULL)
		list_add_tail(&forw_node_new->list, &forw_list);

	batman_if->out.seqno++;


	while ( NULL != ( hashit = hash_iterate( orig_hash, hashit ) ) ) {

		orig_node = hashit->bucket->data;

		debug_output( 4, "count own bcast (schedule_own_packet): old = %i, ", orig_node->bcast_own_sum[batman_if->if_num] );
		bit_get_packet( (TYPE_OF_WORD *)&(orig_node->bcast_own[batman_if->if_num * num_words]), 1, 0 );
		orig_node->bcast_own_sum[batman_if->if_num] = bit_packet_count( (TYPE_OF_WORD *)&(orig_node->bcast_own[batman_if->if_num * num_words]) );
		debug_output( 4, "new = %i \n", orig_node->bcast_own_sum[batman_if->if_num] );

	}

}



void schedule_forward_packet(struct orig_node *orig_node, struct bat_packet *in, uint32_t neigh, uint8_t directlink, unsigned char *hna_recv_buff, int16_t hna_buff_len, struct batman_if *if_outgoing, uint32_t curr_time)
{
	struct forw_node *forw_node_new = NULL, *forw_node_tmp;
	struct list_head *list_pos = forw_list.next, *prev_list_head = (struct list_head *)&forw_list;
	struct bat_packet *bat_packet;
	uint8_t tq_avg = 0;
	prof_start(PROF_schedule_forward_packet);

	debug_output(4, "schedule_forward_packet():  \n");

	if (in->ttl <= 1) {

		debug_output(4, "ttl exceeded \n");
		prof_stop(PROF_schedule_forward_packet);
		return;

	}

	if (aggregation_enabled) {

		list_for_each(list_pos, &forw_list) {

			forw_node_new = list_entry(list_pos, struct forw_node, list);

			/* we can aggregate the current packet to this packet if:
			   - the send time is within our MAX_AGGREGATION_MS time
			   - the resulting packet wont be bigger than MAX_AGGREGATION_BYTES */
			if (((int)(forw_node_new->send_time - (curr_time + MAX_AGGREGATION_MS)) < 0) &&
				(forw_node_new->pack_buff_len + sizeof(struct bat_packet) + hna_buff_len <= MAX_AGGREGATION_BYTES)) {

				bat_packet = (struct bat_packet *)forw_node_new->pack_buff;

				/* if the base packet is sent via one interface only */
				if (((bat_packet->flags & DIRECTLINK) && (bat_packet->ttl == 1)) ||
					((forw_node_new->own) && (forw_node_new->if_outgoing->if_num > 0))) {

					if ((directlink) && (in->ttl == 2) && (forw_node_new->if_outgoing == if_outgoing))
						break;

				} else {

					break;

				}

			}

			forw_node_tmp = forw_node_new;
			forw_node_new = NULL;

			if ((int)(forw_node_tmp->send_time - (curr_time + MAX_AGGREGATION_MS)) > 0)
				break;

			prev_list_head = &forw_node_tmp->list;

		}

	}

	if (forw_node_new == NULL) {

		forw_node_new = debugMalloc(sizeof(struct forw_node), 504);
		forw_node_new->pack_buff = debugMalloc(MAX_AGGREGATION_BYTES, 505);

		INIT_LIST_HEAD(&forw_node_new->list);

		forw_node_new->pack_buff_len = sizeof(struct bat_packet) + hna_buff_len;
		memcpy(forw_node_new->pack_buff, in, forw_node_new->pack_buff_len);

		bat_packet = (struct bat_packet *)forw_node_new->pack_buff;

		forw_node_new->own = 0;
		forw_node_new->if_outgoing = if_outgoing;
		forw_node_new->num_packets = 0;
		forw_node_new->direct_link_flags = 0;

		if (aggregation_enabled)
			forw_node_new->send_time = curr_time + MAX_AGGREGATION_MS;
		else
			forw_node_new->send_time = curr_time;

	} else {

		memcpy(forw_node_new->pack_buff + forw_node_new->pack_buff_len, in, sizeof(struct bat_packet) + hna_buff_len);
		bat_packet = (struct bat_packet *)(forw_node_new->pack_buff + forw_node_new->pack_buff_len);
		forw_node_new->pack_buff_len += sizeof(struct bat_packet) + hna_buff_len;

		forw_node_new->num_packets++;

	}

	/* save packet direct link flag status */
	if (directlink)
		forw_node_new->direct_link_flags = forw_node_new->direct_link_flags | (1 << forw_node_new->num_packets);

	bat_packet->ttl--;
	bat_packet->old_orig = neigh;

	/* rebroadcast tq of our best ranking neighbor to ensure the rebroadcast of our best tq value */
	if ((orig_node->router != NULL) && (orig_node->router->tq_avg != 0)) {

		/* rebroadcast ogm of best ranking neighbor as is */
		if (orig_node->router->addr != neigh) {

			bat_packet->tq = orig_node->router->tq_avg;
			bat_packet->ttl = orig_node->router->last_ttl - 1;

		}

		tq_avg = orig_node->router->tq_avg;

	}

	/* apply hop penalty */
	bat_packet->tq = (bat_packet->tq * (TQ_MAX_VALUE - hop_penalty)) / (TQ_MAX_VALUE);
	debug_output(4, "forwarding: tq_orig: %i, tq_avg: %i, tq_forw: %i, ttl_orig: %i, ttl_forw: %i \n", in->tq, tq_avg, bat_packet->tq, in->ttl - 1, bat_packet->ttl);

	/* change sequence number to network order */
	bat_packet->seqno = htons(bat_packet->seqno);

	if (directlink)
		bat_packet->flags = DIRECTLINK;
	else
		bat_packet->flags = 0x00;

	if (aggregation_enabled) {
		/* new packet was generated and has to be appended */
		if (forw_node_new->num_packets == 0)
			list_add_before(prev_list_head, list_pos, &forw_node_new->list);
	} else {
		list_add(&forw_node_new->list, &forw_list);
	}

	prof_stop(PROF_schedule_forward_packet);
}



void send_outstanding_packets(uint32_t curr_time)
{
	struct forw_node *forw_node;
	struct list_head *forw_pos, *if_pos, *temp;
	struct batman_if *batman_if;
	struct bat_packet *bat_packet;
	char orig_str[ADDR_STR_LEN];
	uint8_t directlink, curr_packet_num;
	int16_t curr_packet_len;

	prof_start(PROF_send_outstanding_packets);


	list_for_each_safe(forw_pos, temp, &forw_list) {

		forw_node = list_entry(forw_pos, struct forw_node, list);

		if ((int)(curr_time - forw_node->send_time) < 0)
			break;

		bat_packet = (struct bat_packet *)forw_node->pack_buff;

		addr_to_string(bat_packet->orig, orig_str, ADDR_STR_LEN);

		directlink = (bat_packet->flags & DIRECTLINK ? 1 : 0);

		if (forw_node->if_outgoing == NULL) {
			debug_output(0, "Error - can't forward packet: outgoing iface not specified \n");
			goto packet_free;
		}

		/* multihomed peer assumed */
		/* non-primary interfaces are only broadcasted on their interface */
		if (((directlink) && (bat_packet->ttl == 1)) ||
			((forw_node->own) && (forw_node->if_outgoing->if_num > 0))) {


			debug_output(4, "%s packet (originator %s, seqno %d, TTL %d) on interface %s\n", (forw_node->own ? "Sending own" : "Forwarding"), orig_str, ntohs(bat_packet->seqno), bat_packet->ttl, forw_node->if_outgoing->dev);

			if (send_udp_packet(forw_node->pack_buff, forw_node->pack_buff_len, &forw_node->if_outgoing->broad, forw_node->if_outgoing->udp_send_sock, forw_node->if_outgoing) < 0)
					deactivate_interface(forw_node->if_outgoing);

			goto packet_free;

		}

		list_for_each(if_pos, &if_list) {

			batman_if = list_entry(if_pos, struct batman_if, list);

			curr_packet_num = curr_packet_len = 0;
			bat_packet = (struct bat_packet *)forw_node->pack_buff;

			while ((curr_packet_len + sizeof(struct bat_packet) <= forw_node->pack_buff_len) &&
				(curr_packet_len + sizeof(struct bat_packet) + bat_packet->hna_len * 5 <= forw_node->pack_buff_len) &&
				(curr_packet_len + sizeof(struct bat_packet) + bat_packet->hna_len * 5 <= MAX_AGGREGATION_BYTES)) {

				if ((forw_node->direct_link_flags & (1 << curr_packet_num)) && (forw_node->if_outgoing == batman_if))
					bat_packet->flags = DIRECTLINK;
				else
					bat_packet->flags = 0x00;

				if (curr_packet_num > 0)
					addr_to_string(bat_packet->orig, orig_str, ADDR_STR_LEN);

				debug_output(4, "%s %spacket (originator %s, seqno %d, TTL %d, IDF %s) on interface %s\n", (curr_packet_num > 0 ? "Forwarding" : (forw_node->own ? "Sending own" : "Forwarding")), (curr_packet_num > 0 ? "aggregated " : ""), orig_str, ntohs(bat_packet->seqno), bat_packet->ttl, batman_if->dev, (bat_packet->flags & DIRECTLINK ? "on" : "off"));

				curr_packet_len += sizeof(struct bat_packet) + bat_packet->hna_len * 5;
				curr_packet_num++;
				bat_packet = (struct bat_packet *)(forw_node->pack_buff + curr_packet_len);

			}

			if (send_udp_packet(forw_node->pack_buff, forw_node->pack_buff_len, &batman_if->broad, batman_if->udp_send_sock, batman_if) < 0)
				deactivate_interface(batman_if);

		}

packet_free:	list_del((struct list_head *)&forw_list, forw_pos, &forw_list);

		if (forw_node->own)
			schedule_own_packet(forw_node->if_outgoing);

		debugFree(forw_node->pack_buff, 1501);
		debugFree(forw_node, 1502);

	}

	prof_stop(PROF_send_outstanding_packets);

}


