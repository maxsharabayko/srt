#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <array>

#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>

#include "srt.h"

using namespace std;

int connect_using_epoll(SRTSOCKET sock, const struct sockaddr* sa, size_t sa_len)
{
    // TODO: Check RCVSYN and SNDSYN
    //srt_getsockflag(sock, SRTO_RCVSYN, &no, sizeof no);
    //srt_getsockflag(sock, SRTO_SNDSYN, &no, sizeof no));

    // TODO: remove epoll
    int epollid = srt_epoll_create();
    if (epollid == -1)
    {
        fprintf(stderr, "srt_epoll_create: %s\n", srt_getlasterror_str());
        return 1;
    }

    // When a caller is connected, a write-readiness event is triggered.
    const int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    if (SRT_ERROR == srt_epoll_add_usock(epollid, sock, &modes))
    {
        fprintf(stderr, "srt_epoll_add_usock: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt connect (non blocking)\n");
    const int st = srt_connect(sock, sa, sa_len);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }

    // We had subscribed for write-readiness or error.
    // Write readiness comes in wready array,
    // error is notified via rready in this case.
    int       rlen = 1;
    SRTSOCKET rready;
    int       wlen = 1;
    SRTSOCKET wready;
    if (srt_epoll_wait(epollid, &rready, &rlen, &wready, &wlen, -1, 0, 0, 0, 0) != -1)
    {
        SRT_SOCKSTATUS state = srt_getsockstate(sock);
        if (state != SRTS_CONNECTED || rlen > 0) // rlen > 0 - an error notification
        {
            fprintf(stderr, "srt_epoll_wait: reject reason %s\n", srt_rejectreason_str(srt_getrejectreason(rready)));
            return 1;
        }

        if (wlen != 1 || wready != sock)
        {
            fprintf(stderr, "srt_epoll_wait: wlen %d, wready %d, socket %d\n", wlen, wready, sock);
            return 1;
        }
    }
    else
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }

    return 0;
}


int main(int argc, char* argv[])
{
    if ((argc != 5) || (0 == atoi(argv[2])))
    {
        cout << "usage: recvfile-epoll server_ip server_port remote_filename local_filename" << endl;
        return -1;
    }

    // use this function to initialize the SRT library
    srt_startup();

    printf("Creating remote address\n");
    const char* remote_ip   = argv[1];
    const char* remote_port = argv[2];
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(remote_port));
    if (inet_pton(AF_INET, remote_ip, &sa.sin_addr) != 1)
    {
        cout << "incorrect server/peer address. " << remote_ip << ":" << remote_port << endl;
        return 1;
    }

    SRTSOCKET client_sock = srt_create_socket();

    const int tt = SRTT_FILE;
    srt_setsockflag(client_sock, SRTO_TRANSTYPE, &tt, sizeof tt);
    const string streamid = string("#!::t=file,m=request,r=") + argv[3];
    srt_setsockflag(client_sock, SRTO_STREAMID, streamid.c_str(), streamid.size());

    printf("srt setsockflag\n");
    const int no = 0;
    if (SRT_ERROR == srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no) ||
        SRT_ERROR == srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no))
    {
        fprintf(stderr, "SRTO_SNDSYN or SRTO_RCVSYN: %s\n", srt_getlasterror_str());
        return 1;
    }

    if (connect_using_epoll(client_sock, (const sockaddr*) &sa, sizeof sa) != 0)
    {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    const int pollid = srt_epoll_create();
    if (pollid)
    {
        fprintf(stderr, "srt_epoll_create: %s\n", srt_getlasterror_str());
        return 1;
    }

    const int epoll_etin = SRT_EPOLL_IN | SRT_EPOLL_ET;
    if (srt_epoll_add_usock(pollid, client_sock, &epoll_etin) == SRT_ERROR)
    {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    std::ofstream targetfile(argv[4], std::ios::out | std::ios::trunc | std::ios::binary);

    std::array<char, 2056> buf;
    for (;;)
    {
        SRTSOCKET read[2], write[2];
        int rlen = 2, wlen = 2;

        const int epoll_res = srt_epoll_wait(pollid, read, &rlen,
            write, &wlen,
            500, // ms
            0, 0, 0, 0);

        if (epoll_res == SRT_ERROR)
        {
            fprintf(stderr, "srt_epoll_wait: %s\n", srt_getlasterror_str());
            break;
        }

        // Handle error notification
        if (rlen > 0 && wlen > 0)
        {
            fprintf(stderr, "srt_epoll_wait: socket status %d\n", srt_getsockstate(read[0]));
            //break;
        }

        if (rlen <= 0)
        {
            fprintf(stderr, "srt_epoll_wait: nothing to read\n");
            continue;
        }

        int num_reads = 0;
        do {
            const int n = srt_recv(client_sock, buf.data(), buf.size());

            if (n == -1)
            {
                if (srt_getlasterror(nullptr) != SRT_EASYNCRCV || num_reads == 0)
                    std::cerr << "srt_recv error: " << srt_getlasterror_str() << ". Reads: " << num_reads << "\n";
                break;
            }

            if (n == 0)
            {
                std::cerr << "Nothing to read after " << num_reads << " reads\n";
                break;
            }

            // Write to file any amount of data received
            targetfile.write(buf.data(), n);
            ++num_reads;
        } while (true);
    }

    if (srt_close(client_sock) == SRT_ERROR)
    {
        std::cerr << "Error closing socket: " << srt_getlasterror_str() << "\n";
    }

    srt_cleanup();
}
