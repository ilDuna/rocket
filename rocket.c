#include "rocket.h"

#include <string.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {
	if (argc > 1) {
		strcmp(argv[1], "-c")==0 ? printf("client mode\n") : 0;
		strcmp(argv[1], "-s")==0 ? printf("server mode\n") : 0;
	}

     

	return 0;
}
