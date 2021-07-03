/* httpserve: serve stdin with custom headers
 *
 * Version 1.0 Jul 2021
 *
 * Copyright (c) 2021, James Barford-Evans
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define TCP_OK  1
#define TCP_ERR 0

#define PORT    8000
#define BACKLOG 100

/* Container for command line arguments */
typedef struct httpOptions {
  int port;
  char *headers;
} httpOptions;

static int httpservePanic(char *fmt, ...) {
  va_list va;
  char msg[BUFSIZ];

  va_start(va, fmt);
  vsnprintf(msg, sizeof(msg), fmt, va);
  fprintf(stderr, "httpserve Error: %s", msg);

  va_end(va);
  exit(EXIT_FAILURE);
  return TCP_ERR;
}

static int tcpCleanupAfterFailure(int sockfd, struct addrinfo *servinfo) {
  if (sockfd != -1)
    (void)close(sockfd);
  freeaddrinfo(servinfo);
  return TCP_ERR;
}

static int setSockReuseAddr(int sockfd) {
  int yes = 1;
  
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    return httpservePanic("failed to set SO_REUSEADDR: %s\n", strerror(errno));

  return TCP_OK;
}

static int tcpListen(int sockfd, struct sockaddr *sa, socklen_t len,
    int backlog) {
  if (bind(sockfd, sa, len) == -1) {
    close(sockfd);
    return httpservePanic("failed to bind to port: %s\n", strerror(errno));
  }

  if (listen(sockfd, backlog) == -1) {
    close(sockfd);
    return httpservePanic("failed to listen: %s\n", strerror(errno));
  }

  return TCP_OK;
}

// Create the server socket
static int tcpServerCreate(int port, int backlog) {
  int sockfd;
  int rv;
  char strport[6];
  struct addrinfo hints, *servinfo, *ptr;

  snprintf(strport, sizeof(strport), "%d", port);
  memset(&hints, '\0', sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, strport, &hints, &servinfo)) != 0)
    return httpservePanic("getaddrinfo failed %s\n", gai_strerror(rv));

  for (ptr = servinfo->ai_next; ptr != NULL; ptr = ptr->ai_next) {
    if ((sockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol))
        == -1)
      continue;

    if (setSockReuseAddr(sockfd) == TCP_ERR)
      return tcpCleanupAfterFailure(sockfd, servinfo);

    if (tcpListen(sockfd, ptr->ai_addr, ptr->ai_addrlen, backlog) == TCP_ERR)
      return tcpCleanupAfterFailure(sockfd, servinfo);

    break;
  }

  if (ptr == NULL) {
    tcpCleanupAfterFailure(sockfd, servinfo);
    return httpservePanic("failed to bind socket\n");
  }

  freeaddrinfo(servinfo);
  return sockfd;
}

// Read all the data from a file descriptor
static char *readAll(int fd) {
  char *buf;
	char tmp[BUFSIZ] = {'\0'};
	int iter = 1;
	int bytes = 0;

	if ((buf = (char *)calloc(sizeof(char *), BUFSIZ)) == NULL)
		return NULL;

	for(;;) {
		iter++;
		bytes = read(fd, tmp, BUFSIZ - 1);
		if (bytes <= 0) break;
		else {
			tmp[bytes] = '\0';
			strcat(buf, tmp);
			char *new_buf = realloc(buf, sizeof(char *) * (iter * BUFSIZ));
			if (new_buf == NULL)
				return NULL;
			buf = new_buf;
		}
	}

	buf[iter * BUFSIZ] = '\0';

	return buf;
}

static void writeResponseToSocket(int sockfd, char *response) {
  int total = 0;
  int len = strlen(response);
  int bytes_left = len;
  int sent = 0;

  while (total < len) {
    sent = send(sockfd, response, bytes_left, 0);

    if (sent == -1)
      httpservePanic("failed to send: %s\n", strerror(errno));

    total += sent;
    bytes_left -= sent;
  }

  len = total;
}

// Compose response to sent to a client
static char *createResponseBuffer(char *body, char *timestamp, char *headers) {
  char *buf;
  int content_len = strlen(body);
  int timestamp_len = strlen(timestamp);

  if ((buf = (char *)calloc(BUFSIZ + content_len + timestamp_len,
          sizeof(char *))) == NULL) {
    httpservePanic("failed to allocate buffer for response: %s\n",
        strerror(errno));
    return NULL;
  }

  sprintf(buf,
      "HTTP/1.1 200 OK\r\n"
      "Date: %s\r\n"
      "Server: httpserve\r\n"
      "%s"
      "Content-Length: %d\r\n\r\n"
      "%s",
      timestamp,
      headers,
      content_len, body);

  return buf;
}

// Send the time as part of the payload
void getServerTime(char *timebuf) {
  time_t raw;
  struct tm *ptm;

  if ((raw = time(NULL)) == -1)
    httpservePanic("failed to set time: %s\n", strerror(errno));

  if ((ptm = localtime(&raw)) == NULL)
    httpservePanic("failed to get localtime: %s\n", strerror(errno));

  strftime(timebuf, BUFSIZ, "%a, %d %b %G %X %Z", ptm);
}

/* get the command line arguments  */
static void getCmdOpts(httpOptions *opts, int argc, char **argv) {
  char arg;

  while ((arg = getopt(argc, argv, "h:p:")) != -1) {
    switch (arg) {
      case 'h': {
        char *ptr = realloc(opts->headers, strlen(opts->headers) + strlen(optarg) * sizeof(char *));
        if (ptr == NULL) {
          httpservePanic("failed to realloc: %s\n", strerror(errno));
        }
          opts->headers = ptr;
        strcat(opts->headers, optarg);
        strcat(opts->headers, "\r\n");
        break;
      }
      case 'p':
        opts->port = atoi(optarg);
        break;
      default:
        httpservePanic("Unknown command line argument: %c\n", arg);
        break;
    }
  }
}

// Read data from stdin and serve it!
int main(int argc, char **argv) {
  char *body, *response;
  int serverfd = -1, newfd = -1;
  struct sockaddr_storage client_addr;
  socklen_t addrlen;
  char timebuf[BUFSIZ] = {'\0'};
  httpOptions opts;
  opts.port = PORT;
  opts.headers = malloc(1);

  getCmdOpts(&opts, argc, argv);

  if ((body = readAll(STDIN_FILENO)) == NULL)
    return httpservePanic("failed to read from stdin\n");

  if ((serverfd = tcpServerCreate(opts.port, BACKLOG)) == TCP_ERR)
    return httpservePanic("failed to create tcp server\n");
  
  printf("Server listening on port: %d\n", opts.port);

  for (;;) {
    if ((newfd = accept(serverfd, (struct sockaddr *)&client_addr,
            &addrlen)) == -1) {
      continue;
    }

    getServerTime(timebuf);

    if ((response = createResponseBuffer(body, timebuf, opts.headers)) == NULL)
      return httpservePanic("failed to create response buffer\n");

    writeResponseToSocket(newfd, response);
    free(response);
  }

  free(body);
  (void)close(serverfd);
  (void)close(newfd);
  exit(EXIT_SUCCESS);
  return 1; 
}
