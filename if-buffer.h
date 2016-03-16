/*
 *	IN-FLIGHT BUFFER
 * a round robin buffer for storing bytes in-flight
 * stored inside tcp recv buffer or send buffer
 */

typedef struct ifbuffer {
	unsigned char *buffer;
	int length;
	int items;
} ifbuffer_t;

ifbuffer_t *ifb_init(int length);
int ifb_push(ifbuffer_t *ifb, char *data, int items);
int ifb_pop(ifbuffer_t *ifb, int items);
unsigned char *ifb_getlastpushed(ifbuffer_t *ifb, int items);
void ifb_free(ifbuffer_t *ifb);
void ifb_print(ifbuffer_t *ifb);