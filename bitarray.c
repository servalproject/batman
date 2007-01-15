#include <stdio.h>		/* printf() */
#include <stdlib.h>		/* malloc(), free() */

#define	BITS_WORD		unsigned int
						/* you should choose something big for the BITS_WORD, 
						 * if you don't want to waste cpu: */

#define	BITS_USED		64
						/* should be a multiple of our word size, 
						 * maybe 32*k, depending on our BITS_WORD */
#define BITS_MAX_SIZE	(BITS_USED/(sizeof(BITS_WORD)*8))
						/* we need an BITS_MAX_SIZE words 
						 * to have store BITS_USED bits. */
#define BITS_WORD_SIZE	(sizeof(BITS_WORD)*8)
						/* our word size */
						
/* the packet bit array. the least signifikant bit of the 0th word is the latest bit, 
 * the highest signifikant bit of bits[ BITS_MAX_SIZE-1 ]; is the oldest word. */
struct bit_packet {
	int last_seq;
	BITS_WORD bits[ BITS_MAX_SIZE ];
};

void bit_init( struct bit_packet *p);
void bit_mark( struct bit_packet *p, int n );
void bit_shift( struct bit_packet *p, int n );
void bit_get_packet( struct bit_packet *p, int seq_num );
int  bit_weight( struct bit_packet *p );

/* above this is prototyping and header stuff, you might want to put this in some .h */
/* implementation: */

/* clear the bits */
void bit_init( struct bit_packet *p) {
	int i;
	
	p->last_seq= -1;	
	for (i=0; i< BITS_MAX_SIZE; i++)
		p->bits[i]= 0;
};

/* turn on bit on, so we can remember that we got the packet */
void bit_mark( struct bit_packet *p, int n ) {
	int word_offset,word_num;
	
	if ( n >= BITS_USED) {			/* if too old, just drop it */	
		printf("got old packet, dropping\n");
		return;
	}
	printf("mark bit %d\n", n);

	word_offset= n%BITS_WORD_SIZE;	/* which position in the selected word */
	word_num   = n/BITS_WORD_SIZE;	/* which word */

	p->bits[word_num]|= 1<<word_offset;	/* turn the position on */
}

/* shift the packet array p by n places. */
void bit_shift( struct bit_packet *p, int n ) {
	int word_offset, word_num;
	int i;


	word_offset= n%BITS_WORD_SIZE;	/* shift how much inside each word */
	word_num   = n/BITS_WORD_SIZE;	/* shift over how much (full) words */

	for ( i=BITS_MAX_SIZE-1; i>word_num; i-- ) { 		
		/* going from old to new, so we can't overwrite the data we copy from. *
 		 * left is high, right is low: FEDC BA98 7654 3210 
		 *	                                  ^^ ^^		  
		 *                             vvvv                
		 * ^^^^ = from, vvvvv =to, we'd have word_num==1 and 
		 * word_offset==BITS_WORD_SIZE/2 in this example. 
		 *
		 * our desired output would be: 9876 5432 1000 0000
		 * */
			
		p->bits[i]= 
			(p->bits[i - word_num] << word_offset) +	
					/* take the lower port from the left half, shift it left to its final position */
			(p->bits[i - word_num - 1] >>	(BITS_WORD_SIZE-word_offset));
					/* and the upper part of the right half and shift it left to it's position */
		/* for our example that would be: word[0] = 9800 + 0076 = 9876 */
	}
	/* now for our last word, i==word_num, we only have the it's "left" half. that's the 1000 word in
	 * our example.*/
	
	p->bits[i]= (p->bits[i - word_num] << word_offset);
	
	/* pad the rest with 0, if there is anything */
	i--;

	for (; i>=0; i--) {
		p->bits[i]= 0;
	}
}

/* print the packet array, for debugging purposes */
void bit_print( struct bit_packet *p ) {
	int i,j;
	
	printf("last sequence number: %d. of the last %d packets, we got %d:\n", p->last_seq, BITS_USED, bit_weight(p));
	for ( i=0; i<BITS_MAX_SIZE; i++ ) {
		for ( j=0; j<BITS_WORD_SIZE; j++) {
			printf("%d", (p->bits[i]>>j)%2 ); /* print the j position */
		}
		printf(" ");
	}
	printf("\n\n");
}

/* receive and process one packet */
void bit_get_packet( struct bit_packet *p, int seq_num ) {
	int diff;
	int i;

	printf("processing packet %d\n", seq_num);

	diff= seq_num - p->last_seq; /* get the difference between the current and the last biggest packet*/

	if (diff < 0) {			/* we already got a sequence number higher
							   than this one, so we just mark it. this should wrap around the integer
							   just fine */
		bit_mark( p, -diff);
		return;
	}

	if (diff > BITS_USED) {		/* it seems we lost a lot of bits or got manipulated, report this. */
		printf("d'oh, BIG loss (missed %d packets)!!\n", diff-1);
		for (i=0; i<BITS_MAX_SIZE; i++) 
			p->bits[i]= 0;
		p->bits[0]=1;			/* we only have the latest packet */
	} else {
		bit_shift(p, diff);
		bit_mark(p, 0);
		
	}
	p->last_seq= seq_num;		/* the sequence number was bigger than the old one, so save it */
}

/* count the hamming weight, how many good packets did we receive? just count the 1's ... */
int bit_weight( struct bit_packet *p ) {
	int i, hamming;
	BITS_WORD word;
	
	hamming=0;
	for (i=0; i<BITS_MAX_SIZE; i++) {
		word= p->bits[i];
		while (word) {
			hamming+= word%2;	/* get the least signifikant 1, if it's there */
			word= word>>1;		/* and shift it aboard. :) */
		}
	}

	return(hamming);
}

/* initializing and some testing */
int main() {
	struct bit_packet *p;
	
	p = (struct bit_packet *) malloc(sizeof(struct bit_packet)); /* allocate one bit array */
	bit_init(p);						/* initialize it */


	/* this is just for testing :) */
	bit_get_packet(p, 0);
	bit_print(p);
	bit_get_packet(p, 1);
	bit_print(p);
	bit_get_packet(p, 7);
	bit_print(p);
	bit_get_packet(p, 5);
	bit_print(p);
	bit_get_packet(p, 6);
	bit_print(p);
	bit_get_packet(p, 35);
	bit_print(p);
	bit_get_packet(p, 33);
	bit_print(p);
	bit_get_packet(p, 2);
	bit_print(p);

	bit_get_packet(p, 127);	/* we had a long, lossy time ... */
	bit_print(p);
	bit_get_packet(p, 0);	/* a very old packet should not bother us */
	bit_print(p);

	/* test the wrap */
	bit_get_packet(p, (1<<(BITS_WORD_SIZE-1))-1 );
	bit_print(p);
	bit_get_packet(p, (1<<(BITS_WORD_SIZE-1)) );
	bit_print(p);	/* should be 110000000000.... now */







	free(p);
	return(0);
}
