/*
 * Copyright (C) 2006 BATMAN contributors:
 * Thomas Lopatic
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
#include "list.h"

void list_init(struct list *list)
{
  list->head = NULL;
  list->tail = NULL;
}

struct list_node *list_get_head(struct list *list)
{
  return list->head;
}

struct list_node *list_get_tail(struct list *list)
{
  return list->tail;
}

struct list_node *list_get_next(struct list_node *node)
{
  return node->next;
}

struct list_node *list_get_prev(struct list_node *node)
{
  return node->prev;
}

void list_add_head(struct list *list, struct list_node *node)
{
  if (list->head != NULL)
    list->head->prev = node;

  else
    list->tail = node;

  node->prev = NULL;
  node->next = list->head;

  list->head = node;
}

void list_add_tail(struct list *list, struct list_node *node)
{
  if (list->tail != NULL)
    list->tail->next = node;

  else
    list->head = node;

  node->prev = list->tail;
  node->next = NULL;

  list->tail = node;
}

void list_add_before(struct list *list, struct list_node *pos_node,
                     struct list_node *node)
{
  if (pos_node->prev != NULL)
    pos_node->prev->next = node;

  else
    list->head = node;

  node->prev = pos_node->prev;
  node->next = pos_node;

  pos_node->prev = node;
}

void list_add_after(struct list *list, struct list_node *pos_node,
                    struct list_node *node)
{
  if (pos_node->next != NULL)
    pos_node->next->prev = node;

  else
    list->tail = node;

  node->prev = pos_node;
  node->next = pos_node->next;

  pos_node->next = node;
}

void list_remove(struct list *list, struct list_node *node)
{
  if (node == list->head)
    list->head = node->next;

  else
    node->prev->next = node->next;

  if (node == list->tail)
    list->tail = node->prev;

  else
    node->next->prev = node->prev;
}
