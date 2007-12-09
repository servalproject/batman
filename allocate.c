/*
 * Copyright (C) 2006 B.A.T.M.A.N. contributors:
 * Thomas Lopatic, Corinna 'Elektra' Aichele, Axel Neumann, Marek Lindner
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



#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os.h"
#include "allocate.h"


#define MAGIC_NUMBER 0x12345678

#if defined DEBUG_MALLOC

struct chunkHeader *chunkList = NULL;

struct chunkHeader
{
	struct chunkHeader *next;
	uint32_t length;
	int32_t tag;
	uint32_t magicNumber;
};

struct chunkTrailer
{
	uint32_t magicNumber;
};



#if defined MEMORY_USAGE

struct memoryUsage *memoryList = NULL;


struct memoryUsage
{
	struct memoryUsage *next;
	uint32_t length;
	uint32_t counter;
	int32_t tag;
};


void addMemory( uint32_t length, int32_t tag ) {

	struct memoryUsage *walker;


	for ( walker = memoryList; walker != NULL; walker = walker->next ) {

		if ( walker->tag == tag ) {

			walker->counter++;
			break;

		}

	}

	if ( walker == NULL ) {

		walker = malloc( sizeof(struct memoryUsage) );

		walker->length = length;
		walker->tag = tag;
		walker->counter = 1;

		walker->next = memoryList;
		memoryList = walker;

	}

}


void removeMemory( int32_t tag, int32_t freetag ) {

	struct memoryUsage *walker;


	for ( walker = memoryList; walker != NULL; walker = walker->next ) {

		if ( walker->tag == tag ) {

			if ( walker->counter == 0 ) {

				debug_output( 0, "Freeing more memory than was allocated: malloc tag = %d, free tag = %d\n", tag, freetag );
				restore_and_exit(0);

			}

			walker->counter--;
			break;

		}

	}

	if ( walker == NULL ) {

		debug_output( 0, "Freeing memory that was never allocated: malloc tag = %d, free tag = %d\n", tag, freetag );
		restore_and_exit(0);

	}

}

#endif



void checkIntegrity(void)
{
	struct chunkHeader *walker;
	struct chunkTrailer *chunkTrailer;
	unsigned char *memory;


#if defined MEMORY_USAGE

	struct memoryUsage *memoryWalker;

	debug_output( 5, " \nMemory usage information:\n" );

	for ( memoryWalker = memoryList; memoryWalker != NULL; memoryWalker = memoryWalker->next ) {

		if ( memoryWalker->counter != 0 )
			debug_output( 5, "   tag: %''4i, num malloc: %4i, bytes per malloc: %''4i, total: %6i\n", memoryWalker->tag, memoryWalker->counter, memoryWalker->length, memoryWalker->counter * memoryWalker->length );

	}

#endif


	for (walker = chunkList; walker != NULL; walker = walker->next)
	{
		if (walker->magicNumber != MAGIC_NUMBER)
		{
			debug_output( 0, "checkIntegrity - invalid magic number in header: %08x, malloc tag = %d\n", walker->magicNumber, walker->tag );
			restore_and_exit(0);
		}

		memory = (unsigned char *)walker;

		chunkTrailer = (struct chunkTrailer *)(memory + sizeof(struct chunkHeader) + walker->length);

		if (chunkTrailer->magicNumber != MAGIC_NUMBER)
		{
			debug_output( 0, "checkIntegrity - invalid magic number in trailer: %08x, malloc tag = %d\n", chunkTrailer->magicNumber, walker->tag );
			restore_and_exit(0);
		}
	}
}

void checkLeak(void)
{
	struct chunkHeader *walker;

	for (walker = chunkList; walker != NULL; walker = walker->next)
		debug_output( 0, "Memory leak detected, malloc tag = %d\n", walker->tag );
}

void *debugMalloc(uint32_t length, int32_t tag)
{
	unsigned char *memory;
	struct chunkHeader *chunkHeader;
	struct chunkTrailer *chunkTrailer;
	unsigned char *chunk;

// 	printf("sizeof(struct chunkHeader) = %u, sizeof (struct chunkTrailer) = %u\n", sizeof (struct chunkHeader), sizeof (struct chunkTrailer));

	memory = malloc(length + sizeof(struct chunkHeader) + sizeof(struct chunkTrailer));

	if (memory == NULL)
	{
		debug_output( 0, "Cannot allocate %u bytes, malloc tag = %d\n", (unsigned int)(length + sizeof(struct chunkHeader) + sizeof(struct chunkTrailer)), tag );
		restore_and_exit(0);
	}

	chunkHeader = (struct chunkHeader *)memory;
	chunk = memory + sizeof(struct chunkHeader);
	chunkTrailer = (struct chunkTrailer *)(memory + sizeof(struct chunkHeader) + length);

	chunkHeader->length = length;
	chunkHeader->tag = tag;
	chunkHeader->magicNumber = MAGIC_NUMBER;

	chunkTrailer->magicNumber = MAGIC_NUMBER;

	chunkHeader->next = chunkList;
	chunkList = chunkHeader;

#if defined MEMORY_USAGE

	addMemory( length, tag );

#endif

	return chunk;
}

void *debugRealloc(void *memoryParameter, uint32_t length, int32_t tag)
{
	unsigned char *memory;
	struct chunkHeader *chunkHeader=NULL;
	struct chunkTrailer *chunkTrailer;
	unsigned char *result;
	uint32_t copyLength;

	if (memoryParameter) { /* if memoryParameter==NULL, realloc() should work like malloc() !! */
		memory = memoryParameter;
		chunkHeader = (struct chunkHeader *)(memory - sizeof(struct chunkHeader));

		if (chunkHeader->magicNumber != MAGIC_NUMBER)
		{
			debug_output( 0, "debugRealloc - invalid magic number in header: %08x, malloc tag = %d\n", chunkHeader->magicNumber, chunkHeader->tag );
			restore_and_exit(0);
		}

		chunkTrailer = (struct chunkTrailer *)(memory + chunkHeader->length);

		if (chunkTrailer->magicNumber != MAGIC_NUMBER)
		{
			debug_output( 0, "debugRealloc - invalid magic number in trailer: %08x, malloc tag = %d\n", chunkTrailer->magicNumber, chunkHeader->tag );
			restore_and_exit(0);
		}
	}


	result = debugMalloc(length, tag);
	if (memoryParameter) {
		copyLength = length;

		if (copyLength > chunkHeader->length)
			copyLength = chunkHeader->length;

		memcpy(result, memoryParameter, copyLength);
		debugFree(memoryParameter, 9999);
	}


	return result;
}

void debugFree(void *memoryParameter, int tag)
{
	unsigned char *memory;
	struct chunkHeader *chunkHeader;
	struct chunkTrailer *chunkTrailer;
	struct chunkHeader *walker;
	struct chunkHeader *previous;

	memory = memoryParameter;
	chunkHeader = (struct chunkHeader *)(memory - sizeof(struct chunkHeader));

	if (chunkHeader->magicNumber != MAGIC_NUMBER)
	{
		debug_output( 0, "debugFree - invalid magic number in header: %08x, malloc tag = %d, free tag = %d\n", chunkHeader->magicNumber, chunkHeader->tag, tag );
		restore_and_exit(0);
	}

	previous = NULL;

	for (walker = chunkList; walker != NULL; walker = walker->next)
	{
		if (walker == chunkHeader)
			break;

		previous = walker;
	}

	if (walker == NULL)
	{
		debug_output( 0, "Double free detected, malloc tag = %d, free tag = %d\n", chunkHeader->tag, tag );
		restore_and_exit(0);
	}

	if (previous == NULL)
		chunkList = walker->next;

	else
		previous->next = walker->next;

	chunkTrailer = (struct chunkTrailer *)(memory + chunkHeader->length);

	if (chunkTrailer->magicNumber != MAGIC_NUMBER)
	{
		debug_output( 0, "debugFree - invalid magic number in trailer: %08x, malloc tag = %d, free tag = %d\n", chunkTrailer->magicNumber, chunkHeader->tag, tag );
		restore_and_exit(0);
	}

#if defined MEMORY_USAGE

	removeMemory( chunkHeader->tag, tag );

#endif

	free(chunkHeader);

}

#else

void checkIntegrity(void)
{
}

void checkLeak(void)
{
}

void *debugMalloc(uint32_t length, int32_t tag)
{
	void *result;

	result = malloc(length);

	if (result == NULL)
	{
		debug_output( 0, "Cannot allocate %u bytes, malloc tag = %d\n", length, tag );
		restore_and_exit(0);
	}

	return result;
}

void *debugRealloc(void *memory, uint32_t length, int32_t tag)
{
	void *result;

	result = realloc(memory, length);

	if (result == NULL)
	{
		debug_output( 0, "Cannot re-allocate %u bytes, malloc tag = %d\n", length, tag );
		restore_and_exit(0);
	}

	return result;
}

void debugFree(void *memory, int32_t tag)
{
	free(memory);
}

#endif
