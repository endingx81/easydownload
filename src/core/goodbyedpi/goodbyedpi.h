#ifndef _GOODBYEDPI_H
#define _GOODBYEDPI_H

#define HOST_MAXLEN 253
#define MAX_PACKET_SIZE 9016

#ifndef DEBUG
#define debug(...) do {} while (0)
#else
#define debug(...) printf(__VA_ARGS__)
#endif

#endif
