#include <stdio.h>
#include <stdlib.h>


#include "srt.h"

using namespace std;

int run_srt(const char*ip, const char * port, int connect_tries, int recv_iterations)
{
	int fd = 0, ret = 0;
	struct addrinfo hints = { 0 }, *ai = NULL, *cur_ai = NULL;

	//struct addrinfo hints = { 0 }, *ai, *cur_ai;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	//hints.ai_family = AF_UNSPEC;
	//hints.ai_socktype = SOCK_DGRAM;
	ret = getaddrinfo(ip, port, &hints, &ai);
	if (ret) {
		printf("error: getaddrinfo returned %d\n", ret);
		return -1;
	}

	cur_ai = ai;

	fd = srt_create_socket();
	if (fd < 0) {
		printf("error: srt_socket %s\n", srt_getlasterror_str());
		return -1;
	}

	ret = -1;
	for (int k = 0; k < connect_tries; k++) {
		// connect to the server, implict bind
		if (SRT_ERROR != srt_connect(fd, cur_ai->ai_addr, cur_ai->ai_addrlen))
		{
			ret = 1;
			break;
		}

		printf("error: srt_connect %s, ip %s, port %s\n",
			srt_getlasterror_str(), ip, port);
		Sleep(1);
	}
	freeaddrinfo(ai);

	if (ret == -1)
	{
		printf("Filed to connect\n");
		srt_close(fd);
		return -1;
	}

	for (int k = 0; k < recv_iterations; k++) {

		char buff[4096];
		ret = srt_recvmsg(fd, buff, sizeof(buff));
		if (SRT_ERROR == ret)
		{
			printf("error: srt_recvmsg %s\n", srt_getlasterror_str());
			ret = -1;
			break;
		}
		printf("succeed to recv msg %d\n", ret);
	};

	srt_close(fd);
	return 0;
}


int main(int argc, char* argv[])
{
	int ret = 0;
	if ((argc != 3) || (0 == atoi(argv[2])))
	{
		printf("usage: test_srt server_ip server_port");
		return -1;
	}

	if (srt_startup() < 0) {
		 printf("error: srt_startup %s\n", srt_getlasterror_str());
		 return -1;
	}

	run_srt(argv[1], argv[2], 1000, 2000000000);

	srt_cleanup();
}
