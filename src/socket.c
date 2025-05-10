
#include "socket.h"

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif 

#define BUF_SIZE 5000
#if 0
var socket__new(var host, var port)
{
		int argc = 3;
		char *argv[4];

           int              sfd, s;
           char             buf[BUF_SIZE];
           size_t           size;
           u_long           mode;
           ssize_t          nread;
           struct addrinfo  hints;
           struct addrinfo  *result, *rp;
	argv[0] = "os-3o3";
	argv[1] = (char*)host;
	argv[2] = (char*)port;
           if (argc < 3) {
               fprintf(stderr, "Usage: %s host port msg...\n", argv[0]);
               exit(EXIT_FAILURE);
           }
#ifdef _WIN32
WSADATA wsaData;
if ( WSAStartup(MAKEWORD(2,2), &wsaData) != NO_ERROR) {
	printf("Error at WSAStartup()\n");
               exit(EXIT_FAILURE);

}
#endif

           /* Obtain address(es) matching host/port. */

           memset(&hints, 0, sizeof(hints));
           hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
           hints.ai_socktype = SOCK_DGRAM; /* UDP */
           hints.ai_socktype = SOCK_STREAM; /* TCP */
           hints.ai_flags = 0;
           hints.ai_protocol = 0;          /* Any protocol */

           s = getaddrinfo(argv[1], argv[2], &hints, &result);
           if (s != 0) {
               fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
               exit(EXIT_FAILURE);
           }

           /* getaddrinfo() returns a list of address structures.
              Try each address until we successfully connect(2).
              If socket(2) (or connect(2)) fails, we (close the socket
              and) try the next address. */

           for (rp = result; rp != NULL; rp = rp->ai_next) {
               sfd = socket(rp->ai_family, rp->ai_socktype,
                            rp->ai_protocol);
               if (sfd == -1)
                   continue;

               if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
                   break;                  /* Success */

               close(sfd);
           }

           freeaddrinfo(result);           /* No longer needed */

           if (rp == NULL) {               /* No address succeeded */
              fprintf(stderr, "Could not connect\n");
               exit(EXIT_FAILURE);
           }
	   mode = 1;
#ifdef _WIN32
	   if (ioctlsocket(sfd, FIONBIO, &mode) != NO_ERROR) {
#else 
	   if (ioctl(sfd, FIONBIO, &mode)) {
#endif
              fprintf(stderr, "Could not non-blocking\n");
               exit(EXIT_FAILURE);
	   }

           /* Send remaining command-line arguments as separate
              datagrams, and read responses from server. */

           for (size_t j = 3; j < argc; j++) {
               size = strlen(argv[j]) + 1;
                       /* +1 for terminating null byte */

               if (size > BUF_SIZE) {
                   fprintf(stderr,
                           "Ignoring long message in argument %zu\n", j);
                   continue;
               }

               if (write(sfd, argv[j], size) != size) {
                   fprintf(stderr, "partial/failed write\n");
                   exit(EXIT_FAILURE);
               }

               nread = read(sfd, buf, BUF_SIZE);
               if (nread == -1) {
                   perror("read");
                   exit(EXIT_FAILURE);
               }

               printf("Received %zd bytes: %s\n", nread, buf);
           }

#ifdef _WIN32
WSACleanup();
#endif
           exit(EXIT_SUCCESS);
       }

#undef BUF_SIZE 

#endif
