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
	ifb->items = 0;
	return ifb;
}

int ifb_push(ifbuffer_t *ifb, char *data, int items) {
	if (items >= ifb->length) {
		bzero(ifb->buffer, ifb->length);
		memcpy(ifb->buffer, data + (items - ifb->length), ifb->length);
		ifb->items = ifb->length;
	}
	else if (items + ifb->items > ifb->length) {
		ifb_pop(ifb, (items + ifb->items) - ifb->length);
		memcpy(ifb->buffer + ifb->items, data, items);
		ifb->items += items;
	}
	else {
		memcpy(ifb->buffer + ifb->items, data, items);
		ifb->items += items;
	}
	return 0;
}

int ifb_pop(ifbuffer_t *ifb, int items) {
	if (ifb->length == 0 ||
		items > ifb->items ||
		items > ifb->length)
		return -1;
	bzero(ifb->buffer, items);
	ifb->items -= items;
	int new_pos = 0;
	for (int cur_pos = items; cur_pos < ifb->length; cur_pos++) {
		ifb->buffer[new_pos] = ifb->buffer[cur_pos];
		new_pos++;
	}
	bzero(ifb->buffer + ifb->items, ifb->length - ifb->items);
	return 0;
}

void ifb_free(ifbuffer_t *ifb) {
	free(ifb->buffer);
	free(ifb);
}

void ifb_print(ifbuffer_t *ifb) {
	printf("in-flight buffer (size %d, items %d):\n", ifb->length, ifb->items);
	for (int i = 0; i < ifb->length; i++) {
		if (ifb->buffer[i] == 0)
			printf("_ ");
		else
			printf("%c ", ifb->buffer[i]);
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