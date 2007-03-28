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


#include <stdio.h>		/* NULL */
#include "hash.h"
#include "allocate.h"


/* clears the hash */
void hash_init(struct hashtable_t *hash) {
	int i;
	hash->elements=0;
	for (i=0 ; i<hash->size ; i++) {
		hash->table[i].data= NULL;
		hash->table[i].next= NULL;
		hash->table[i].prev= NULL;
	}
}


/* remove the hash structure. if hashdata_free_cb != NULL,
 * this function will be called to remove the elements inside of the hash.
 * if you don't remove the elements, memory might be leaked. */
void hash_delete(struct hashtable_t *hash, hashdata_free_cb free_cb) {
	struct element_t *bucket, *last_bucket;
	int i;

	for (i=0; i<hash->size; i++) {
		if (hash->table[i].data != NULL) {
			if (free_cb!=NULL) free_cb( hash->table[i].data );

			bucket= hash->table[i].next;
			while (bucket != NULL) {
				if (free_cb!=NULL) free_cb( bucket->data );
				last_bucket= bucket;
				bucket= bucket->next;
				debugFree(last_bucket, 1301);
			}
		}
	}
	hash_destroy(hash);
}



/* free only the hashtable and the hash itself. */
void hash_destroy(struct hashtable_t *hash) {

	debugFree( hash->table, 1302 );
	debugFree( hash, 1303 );

}




/* iterate though the hash. first element is selected with iter_in NULL.
 * use the returned iterator to access the elements until hash_it_t returns NULL. */
struct hash_it_t *hash_iterate(struct hashtable_t *hash, struct hash_it_t *iter_in) {
	struct hash_it_t *iter;

	if (iter_in == NULL) {
		iter= debugMalloc(sizeof(struct hash_it_t), 301);
		iter->index =  -1;
		iter->bucket = NULL;
		iter->last_bucket = NULL;
		iter->next_bucket = NULL;
	} else
		iter= iter_in;

	if (iter->bucket!=NULL) {
		/* sanity checks first (if our bucket got deleted in the last iteration): */
		if (iter->last_bucket == NULL) {
			if (iter->next_bucket != iter->bucket->next) {
				/* we're on the first element and it got removed after the last iteration. 
				 * the bucket did not change, but the next pointer did, means the next bucket got
				 * copied to the first place. */
				iter->next_bucket = iter->bucket->next;
				return(iter);
			}	
		} else {
			if (iter->last_bucket->next != iter->bucket) {
				/* we're not on the first element, and the bucket got removed after the last iteration.
				 * the last bucket's next pointer is not pointing to our actual bucket anymore. 
				 * select the next. */
				iter->bucket= iter->last_bucket->next;
				if (iter->bucket!=NULL) {
					iter->next_bucket = iter->bucket->next;
					return (iter);
				} else {
					iter->next_bucket=NULL;
					/* it was the last element which got deleted, continue with searching. */
				}
			}
		}

	}

	/* now as we are sane, select the next one if there is some */
	if (iter->bucket!=NULL) {
		if (iter->bucket->next!=NULL) {
			iter->last_bucket= iter->bucket;
			iter->bucket= iter->bucket->next;
			iter->next_bucket= iter->bucket->next;
			return(iter);
		}
	}
	/* if not returned yet, we've reached the last one on the index and have to search forward */

	iter->index++;
	while ( iter->index < hash->size ) {		/* go through the entries of the hash table */
		if ((hash->table[ iter->index ].data) != NULL){
			iter->bucket = &(hash->table[ iter->index ]);
			iter->next_bucket = iter->bucket->next;
			iter->last_bucket = NULL;
			return(iter);						/* if this table entry is not null, return it */
		} else
			iter->index++;						/* else, go to the next */
	}
	/* nothing to iterate over anymore */
	debugFree(iter, 1304);
	return(NULL);
}


/* allocates and clears the hash */
struct hashtable_t *hash_new(int size, hashdata_compare_cb compare, hashdata_choose_cb choose) {
	struct hashtable_t *hash;

	hash= debugMalloc( sizeof(struct hashtable_t) , 302);
	if ( hash == NULL ) 			/* could not allocate the hash control structure */
		return (NULL);

	hash->size= size;
	hash->table= debugMalloc( sizeof(struct element_t) * size, 303);
	if ( hash->table == NULL ) {	/* could not allocate the table */
		debugFree(hash, 1305);
		return(NULL);
	}
	hash->compare= compare;
	hash->choose= choose;
	return(hash);
}


/* adds data to the hashtable. returns 0 on success, -1 on error */
int hash_add(struct hashtable_t *hash, void *data) {
	int index;
	struct element_t *bucket, *new_bucket;

	index = hash->choose( data , hash->size );
	bucket = &(hash->table[index]);

	if ( bucket->data==NULL ) {		/* bucket is empty, put it in */
		bucket->data=data;
		hash->elements++;
		return(0);
	} else {
		new_bucket= bucket;
		do {
			if (0 == hash->compare( new_bucket->data, data )) {		/* already added, don't add again */
				return(-1);
			}
			bucket= new_bucket;
			new_bucket= bucket->next;
		} while ( new_bucket!=NULL);

		/* found the tail of the list, add new element */
		if (NULL == (new_bucket= debugMalloc(sizeof(struct element_t),304)))
			return(-1); /* debugMalloc failed */

		new_bucket->data= data;				/* init the new bucket */
		new_bucket->next= NULL;
		new_bucket->prev= bucket;
		bucket->next=     new_bucket;		/* and link it */
		hash->elements++;
		return(0);

	}
}
/* finds data, based on the key in keydata. returns the found data on success, or NULL on error */
void *hash_find(struct hashtable_t *hash, void *keydata) {
	int index;
	struct element_t *bucket;

	index = hash->choose( keydata , hash->size );
	bucket = &(hash->table[index]);

	if ( bucket->data!=NULL ) {
		do {
			if (0 == hash->compare( bucket->data, keydata )) {
				return( bucket->data );
			}
			bucket= bucket->next;
		} while ( bucket!=NULL );
	}
	return(NULL);

}

/* remove bucket (this might be used in hash_iterate() if you already found the bucket
 * you want to delete and don't need the overhead to find it again with hash_remove(). 
 * But usually, you don't want to use this function, as it fiddles with hash-internals. */
void *hash_remove_bucket(struct hashtable_t *hash, struct element_t *bucket) {
	struct element_t *next_bucket;
	void *data_save;

	data_save = bucket->data;						/* save the pointer to the data */
	if ( bucket->prev == NULL ) { 					/* we're on the first entry */
		if ( bucket->next == NULL ) { 				/* there is no next bucket, nothing to preserve. */
			bucket->data= NULL;
		} else {									/* else, move the second bucket onto the first one */
			next_bucket = bucket->next;
			bucket->data= bucket->next->data;
			bucket->next= bucket->next->next;
			debugFree(next_bucket, 1306);			/* free the next_bucket, as we copied its data into our
						 							 * first bucket. */
			if (bucket->next!=NULL) {				/* 3rd bucket would point on the removed bucket. fix this. */
				bucket->next->prev= bucket;
			}
		}
	} else { /* not the first entry */
		if (bucket->next!=NULL)
			bucket->next->prev = bucket->prev;		/* repair link on the next */
		bucket->prev->next = bucket->next;			/* and on the previous bucket. */
		debugFree(bucket, 1307);
	}

	hash->elements--;
	return( data_save );

}


/* removes data from hash, if found. returns pointer do data on success,
 * so you can remove the used structure yourself, or NULL on error .
 * data could be the structure you use with just the key filled,
 * we just need the key for comparing. */
void *hash_remove(struct hashtable_t *hash, void *data) {
	int index;
	struct element_t *bucket;

	index = hash->choose( data , hash->size );
	bucket = &(hash->table[index]);

	if (bucket->data != NULL) {
		do {
			if (0 == hash->compare( bucket->data, data )) {		/* found entry, delete it. */
				return( hash_remove_bucket(hash, bucket));
			}
			bucket=      bucket->next;
		} while (bucket!=NULL);
	}

	return(NULL);
}


/* resize the hash, returns the pointer to the new hash or NULL on error. removes the old hash on success. */
struct hashtable_t *hash_resize(struct hashtable_t *hash, int size) {
	struct hashtable_t *new_hash;
	struct element_t *bucket;
	int i;

	/* initialize a new hash with the new size */
	if (NULL == (new_hash= hash_new(size, hash->compare, hash->choose)))
		return(NULL);

	/* copy the elements */
	for (i=0; i<hash->size; i++) {
		if (hash->table[i].data != NULL) {
			hash_add( new_hash, hash->table[i].data );
			bucket= hash->table[i].next;
			while (bucket != NULL) {
				hash_add( new_hash, bucket->data );
				bucket= bucket->next;
			}
		}
	}
	hash_delete(hash, NULL );	/* remove hash and eventual overflow buckets,
								 * but not the content itself. */

	return( new_hash);

}


/* print the hash table for debugging */
void hash_debug(struct hashtable_t *hash) {
	int i;
	struct element_t *bucket;
	for (i=0; i<hash->size;i++) {
/* 		printf("[%d] ",i); */
		if (hash->table[i].data != NULL) {
			printf("[%d] [%10p] ", i, hash->table[i].data);

			bucket= hash->table[i].next;
			while (bucket != NULL) {
				printf("-> [%10p] ", bucket->data);
				bucket= bucket->next;
			}
			printf("\n");
		}

	}
}

