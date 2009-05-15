/*
 * Copyright (C) 2006-2009 B.A.T.M.A.N. contributors:
 *
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



#include "hna.h"
#include "os.h"

#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>


unsigned char *hna_buff_local = NULL;
uint8_t num_hna_local = 0;

struct list_head_first hna_list;
struct list_head_first hna_chg_list;

pthread_mutex_t hna_chg_list_mutex;


void hna_init(void)
{
	INIT_LIST_HEAD_FIRST(hna_list);
	INIT_LIST_HEAD_FIRST(hna_chg_list);

	pthread_mutex_init(&hna_chg_list_mutex, NULL);
}

void hna_free(void)
{
	struct list_head *list_pos, *list_pos_tmp;
	struct hna_node *hna_node;

	list_for_each_safe(list_pos, list_pos_tmp, &hna_list) {
		hna_node = list_entry(list_pos, struct hna_node, list);
		hna_local_update_routes(hna_node, ROUTE_DEL);

		debugFree(hna_node, 1103);
	}

	if (hna_buff_local != NULL)
		debugFree(hna_buff_local, 1104);
}

/* this function can be called when the daemon starts or at runtime */
void hna_local_task_add_ip(uint32_t ip_addr, uint16_t netmask, uint8_t route_action)
{
	struct hna_node *hna_node;

	hna_node = debugMalloc(sizeof(struct hna_node), 203);
	memset(hna_node, 0, sizeof(struct hna_node));
	INIT_LIST_HEAD(&hna_node->list);

	hna_node->addr = ip_addr;
	hna_node->netmask = netmask;
	hna_node->route_action = route_action;

	if (pthread_mutex_lock(&hna_chg_list_mutex) != 0)
		debug_output(0, "Error - could not lock hna_chg_list mutex in %s(): %s \n", __func__, strerror(errno));

	list_add_tail(&hna_node->list, &hna_chg_list);

	if (pthread_mutex_unlock(&hna_chg_list_mutex) != 0)
		debug_output(0, "Error - could not unlock hna_chg_list mutex in %s(): %s \n", __func__, strerror(errno));
}

/* this function can be called when the daemon starts or at runtime */
void hna_local_task_add_str(char *hna_string, uint8_t route_action, uint8_t runtime)
{
	struct in_addr tmp_ip_holder;
	uint16_t netmask;
	char *slash_ptr;

	if ((slash_ptr = strchr(hna_string, '/')) == NULL) {

		if (runtime) {
			debug_output(3, "Invalid announced network (netmask is missing): %s\n", hna_string);
			return;
		}

		printf("Invalid announced network (netmask is missing): %s\n", hna_string);
		exit(EXIT_FAILURE);
	}

	*slash_ptr = '\0';

	if (inet_pton(AF_INET, hna_string, &tmp_ip_holder) < 1) {

		*slash_ptr = '/';

		if (runtime) {
			debug_output(3, "Invalid announced network (IP is invalid): %s\n", hna_string);
			return;
		}

		printf("Invalid announced network (IP is invalid): %s\n", hna_string);
		exit(EXIT_FAILURE);
	}

	errno = 0;
	netmask = strtol(slash_ptr + 1, NULL, 10);

	if ((errno == ERANGE) || (errno != 0 && netmask == 0)) {

		*slash_ptr = '/';

		if (runtime)
			return;

		perror("strtol");
		exit(EXIT_FAILURE);
	}

	if (netmask < 1 || netmask > 32) {

		*slash_ptr = '/';

		if (runtime) {
			debug_output(3, "Invalid announced network (netmask is invalid): %s\n", hna_string);
			return;
		}

		printf("Invalid announced network (netmask is invalid): %s\n", hna_string);
		exit(EXIT_FAILURE);
	}

	*slash_ptr = '/';

	tmp_ip_holder.s_addr = (tmp_ip_holder.s_addr & htonl(0xFFFFFFFF << (32 - netmask)));
	hna_local_task_add_ip(tmp_ip_holder.s_addr, netmask, route_action);
}

static void hna_local_buffer_fill(void)
{
	struct list_head *list_pos;
	struct hna_node *hna_node;

	if (hna_buff_local != NULL)
		debugFree(hna_buff_local, 1111);

	num_hna_local = 0;
	hna_buff_local = NULL;

	if (list_empty(&hna_list))
		return;

	list_for_each(list_pos, &hna_list) {
		hna_node = list_entry(list_pos, struct hna_node, list);
		hna_buff_local = debugRealloc(hna_buff_local, (num_hna_local + 1) * 5 * sizeof(unsigned char), 15);

		memmove(&hna_buff_local[num_hna_local * 5], (unsigned char *)&hna_node->addr, 4);
		hna_buff_local[(num_hna_local * 5) + 4] = (unsigned char)hna_node->netmask;
		num_hna_local++;
	}
}

void hna_local_task_exec(void)
{
	struct list_head *list_pos, *list_pos_tmp, *prev_list_head;
	struct list_head *hna_pos, *hna_pos_tmp;
	struct hna_node *hna_node, *hna_node_exist;
	char hna_addr_str[ADDR_STR_LEN];

	if (pthread_mutex_trylock(&hna_chg_list_mutex) != 0)
		return;

	if (list_empty(&hna_chg_list))
		goto unlock_chg_list;

	list_for_each_safe(list_pos, list_pos_tmp, &hna_chg_list) {

		hna_node = list_entry(list_pos, struct hna_node, list);
		addr_to_string(hna_node->addr, hna_addr_str, sizeof(hna_addr_str));

		hna_node_exist = NULL;
		prev_list_head = (struct list_head *)&hna_list;

		list_for_each_safe(hna_pos, hna_pos_tmp, &hna_list) {

			hna_node_exist = list_entry(hna_pos, struct hna_node, list);

			if ((hna_node->addr == hna_node_exist->addr) && (hna_node->netmask == hna_node_exist->netmask)) {

				if (hna_node->route_action == ROUTE_DEL) {
					debug_output(3, "Deleting HNA from announce network list: %s/%i\n", hna_addr_str, hna_node->netmask);

					hna_local_update_routes(hna_node, ROUTE_DEL);
					list_del(prev_list_head, hna_pos, &hna_list);

					debugFree(hna_node_exist, 1109);
				} else {
					debug_output(3, "Can't add HNA - already announcing network: %s/%i\n", hna_addr_str, hna_node->netmask);
				}

				break;

			}

			prev_list_head = &hna_node_exist->list;
			hna_node_exist = NULL;

		}

		if (hna_node_exist == NULL) {

			if (hna_node->route_action == ROUTE_ADD) {
				debug_output(3, "Adding HNA to announce network list: %s/%i\n", hna_addr_str, hna_node->netmask);

				hna_local_update_routes(hna_node, ROUTE_ADD);

				/* add node */
				hna_node_exist = debugMalloc(sizeof(struct hna_node), 105);
				memset(hna_node_exist, 0, sizeof(struct hna_node));
				INIT_LIST_HEAD(&hna_node_exist->list);

				hna_node_exist->addr = hna_node->addr;
				hna_node_exist->netmask = hna_node->netmask;

				list_add_tail(&hna_node_exist->list, &hna_list);
			} else {
				debug_output(3, "Can't delete HNA - network is not announced: %s/%i\n", hna_addr_str, hna_node->netmask);
			}

		}

		list_del((struct list_head *)&hna_chg_list, list_pos, &hna_chg_list);
		debugFree(hna_node, 1110);

	}

	/* rewrite local buffer */
	hna_local_buffer_fill();

unlock_chg_list:
	if (pthread_mutex_unlock(&hna_chg_list_mutex) != 0)
		debug_output(0, "Error - could not unlock hna_chg_list mutex in %s(): %s \n", __func__, strerror(errno));
}

void hna_local_update_vis_packet(unsigned char *vis_packet, uint16_t *vis_packet_size)
{
	struct list_head *list_pos;
	struct hna_node *hna_node;
	struct vis_data *vis_data;

	if (num_hna_local < 1)
		return;

	list_for_each(list_pos, &hna_list) {
		hna_node = list_entry(list_pos, struct hna_node, list);

		*vis_packet_size += sizeof(struct vis_data);

		vis_packet = debugRealloc(vis_packet, *vis_packet_size, 107);
		vis_data = (struct vis_data *)(vis_packet + *vis_packet_size - sizeof(struct vis_data));

		memcpy(&vis_data->ip, (unsigned char *)&hna_node->addr, 4);

		vis_data->data = hna_node->netmask;
		vis_data->type = DATA_TYPE_HNA;
	}
}

void hna_local_update_routes(struct hna_node *hna_node, int8_t route_action)
{
	/* add / delete throw routing entries for own hna */
	add_del_route(hna_node->addr, hna_node->netmask, 0, 0, 0, "unknown", BATMAN_RT_TABLE_NETWORKS, ROUTE_TYPE_THROW, route_action);
	add_del_route(hna_node->addr, hna_node->netmask, 0, 0, 0, "unknown", BATMAN_RT_TABLE_HOSTS, ROUTE_TYPE_THROW, route_action);
	add_del_route(hna_node->addr, hna_node->netmask, 0, 0, 0, "unknown", BATMAN_RT_TABLE_UNREACH, ROUTE_TYPE_THROW, route_action);
	add_del_route(hna_node->addr, hna_node->netmask, 0, 0, 0, "unknown", BATMAN_RT_TABLE_TUNNEL, ROUTE_TYPE_THROW, route_action);

	/* do not NAT HNA networks automatically */
	hna_local_update_nat(hna_node->addr, hna_node->netmask, route_action);
}

/* hna_buff_delete searches in buf if element e is found.
 *
 * if found, delete it from the buf and return 1.
 * if not found, return 0.
 */
static int hna_buff_delete(struct hna_element *buf, int *buf_len, struct hna_element *e)
{
	int i;
	int num_elements;

	if (buf == NULL)
		return 0;

	/* align to multiple of sizeof(struct hna_element) */
	num_elements = *buf_len / sizeof(struct hna_element);

	for (i = 0; i < num_elements; i++) {

		if (memcmp(&buf[i], e, sizeof(struct hna_element)) == 0) {

			/* move last element forward */
			memmove(&buf[i], &buf[num_elements - 1], sizeof(struct hna_element));
			*buf_len -= sizeof(struct hna_element);

			return 1;
		}
	}
	return 0;
}

/* update_hna_global() replaces the old add_del_hna function. This function
 * updates the new hna buffer for the supplied orig node and
 * adds/deletes/updates the announced routes.
 *
 * Instead of first deleting and then adding, we try to add new routes
 * before delting the old ones so that the kernel will not experience
 * a situation where no route is present.
 */
void hna_global_update(struct orig_node *orig_node, unsigned char *new_hna,
				int16_t new_hna_len, struct neigh_node *old_router)
{
	unsigned char *old_hna;
	int old_hna_len;
	struct hna_element *e, *buf;
	int i, num_elements;

	if ((new_hna == NULL) && (new_hna_len != 0)) {
		/* TODO: throw error, broken! */
		return;
	}

	old_hna = orig_node->hna_buff;
	old_hna_len = orig_node->hna_buff_len;

	/* check if the buffers even changed. if its still the same, there is no need to
	 * update the routes. if the router changed, then we have to update all the routes */

	/* note: no NULL pointer checking here because memcmp() just returns if n == 0 */
	if ((old_router == orig_node->router) &&
		(old_hna_len == new_hna_len) && (memcmp(old_hna, new_hna, new_hna_len) == 0))
		return;	/* nothing to do */

	if (new_hna_len != 0) {

		orig_node->hna_buff = debugMalloc(new_hna_len, 101);
		orig_node->hna_buff_len = new_hna_len;
		memcpy(orig_node->hna_buff, new_hna, new_hna_len);

	} else {

		orig_node->hna_buff = NULL;
		orig_node->hna_buff_len = 0;

	}

	/* add new routes, or keep old routes */
	num_elements = orig_node->hna_buff_len / sizeof(struct hna_element);
	buf = (struct hna_element *)orig_node->hna_buff;

	for (i = 0; i < num_elements; i++) {
		e = &buf[i];

		/* if the router changed, we have to add the new route in either case.
		 * if the router is the same, and the announcement was already in the old
		 * buffer, we can keep the route.
		 *
		 * NOTE: if the router changed, hna_buff_delete() is not called. */

		if ((old_router != orig_node->router)
			|| (hna_buff_delete((struct hna_element *)old_hna, &old_hna_len, e) == 0)) {

			/* not found / deleted, need to add this new route */
			if ((e->netmask > 0) && (e->netmask <= 32))
				add_del_route(e->hna, e->netmask, orig_node->router->addr, orig_node->router->if_incoming->addr.sin_addr.s_addr,
						orig_node->router->if_incoming->if_index, orig_node->router->if_incoming->dev,
						BATMAN_RT_TABLE_NETWORKS, ROUTE_TYPE_UNICAST, ROUTE_ADD);

		}
	}

	/* old routes which are not to be kept are deleted now. */
	num_elements = old_hna_len / sizeof(struct hna_element);
	buf = (struct hna_element *)old_hna;

	for (i = 0; i < num_elements; i++) {
		e = &buf[i];

		if ((e->netmask > 0) && (e->netmask <= 32) && (old_router != NULL))
			add_del_route(e->hna, e->netmask, old_router->addr, old_router->if_incoming->addr.sin_addr.s_addr,
						old_router->if_incoming->if_index, old_router->if_incoming->dev, BATMAN_RT_TABLE_NETWORKS, ROUTE_TYPE_UNICAST, ROUTE_DEL);
	}

	/* dispose old hna buffer now. */
	if (old_hna != NULL)
		debugFree(old_hna, 1101);

}
