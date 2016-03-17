/*
 *	IN-FLIGHT BUFFER
 * a round robin buffer for storing bytes in-flight
 * stored inside tcp recv buffer or send buffer
 */

#include "if-buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

ifbuffer_t *ifb_init(int length) {
	ifbuffer_t *ifb = malloc(sizeof(ifbuffer_t));
	ifb->buffer = malloc(length);
	bzero(ifb->buffer, length);
	ifb->length = length;
	ifb->position = 0;
	return ifb;
}

int ifb_push(ifbuffer_t *ifb, char *data, int items) {
	int space = ifb->length - ifb->position;
	if (items <= space) {
		memcpy(ifb->buffer + ifb->position, data, items);
		ifb->position = (ifb->position + items) % ifb->length;
	}
	else {
		memcpy(ifb->buffer + ifb->position, data, space);
		ifb->position = 0;
		ifb_push(ifb, data + space, items - space);
	}
	return 0;
}

unsigned char *ifb_getlastpushed(ifbuffer_t *ifb, int items) {
	unsigned char *lastpushed = malloc(items);
	if (items <= ifb->position) {
		memcpy(lastpushed, ifb->buffer + ifb->position - items, items);
	}
	else {
		int over = items % ifb->position;
		memcpy(lastpushed, ifb->buffer + ifb->length - over, over);
		memcpy(lastpushed + over, ifb->buffer, ifb->position);
	}
	return lastpushed;
}

void ifb_free(ifbuffer_t *ifb) {
	free(ifb->buffer);
	free(ifb);
}

void ifb_print(ifbuffer_t *ifb) {
	printf("in-flight buffer (length %d):\n", ifb->length);
	int i = 0;
	for (i = 0; i < ifb->length; i++) {
		if (ifb->buffer[i] == 0)
			printf("_ ");
		else
			printf("%c ", ifb->buffer[i]);
	}
	printf("\n");
	for (i = 0; i < ifb->length; i++) {
		if (i == ifb->position)
			printf("^ ");
		else
			printf("  ");
	}
	printf("\n");
}

/*
int main(int argc, char **argv) {
	char *data = "ABCDEFGHILMNOPQRSTUVZ";
	ifbuffer_t *ring = ifb_init(9);
	ifb_print(ring);
	ifb_push(ring, data, 4);
	ifb_print(ring);
	ifb_push(ring, data, 2);
	ifb_print(ring);
	ifb_push(ring, data, 15);
	ifb_print(ring);
	ifb_free(ring);
	return 0;
}*/