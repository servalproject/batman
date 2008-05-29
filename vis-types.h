/*
 * vis-types.h
 *
 * Copyright (C) 2006 Marek Lindner:
 *
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



#include <stdint.h>



#define DATA_TYPE_NEIGH 1
#define DATA_TYPE_SEC_IF 2
#define DATA_TYPE_HNA 3



struct vis_packet {
	uint32_t sender_ip;
	uint8_t version;
	uint8_t gw_class;
	uint8_t tq_max;
} __attribute__((packed));

struct vis_data {
	uint8_t type;
	uint8_t data;
	uint32_t ip;
} __attribute__((packed));

