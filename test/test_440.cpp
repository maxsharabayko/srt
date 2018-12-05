/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 * Written by:
 *             Haivision Systems Inc.
 */

#include <gtest/gtest.h>


#define WIN32 1
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "srt.h"
//#include "../srt-master/apps/apputil.hpp"

#include <thread>

#pragma comment (lib, "ws2_32.lib")

inline sockaddr_in CreateAddrInet(const std::string& name, unsigned short port)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (name != "")
    {
        if (inet_pton(AF_INET, name.c_str(), &sa.sin_addr) == 1)
            return sa;

        // XXX RACY!!! Use getaddrinfo() instead. Check portability.
        // Windows/Linux declare it.
        // See:
        //  http://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancedInternet3b.html
        hostent* he = gethostbyname(name.c_str());
        if (!he || he->h_addrtype != AF_INET)
            throw std::invalid_argument("SrtSource: host not found: " + name);

        sa.sin_addr = *(in_addr*)he->h_addr_list[0];
    }

    return sa;
}


TEST(Performance, Test440)
{
    srt_startup();

    auto server = std::thread([] {

        int server = srt_create_socket();

        const int yes = 1;
        const int no = 0;
        srt_setsockopt(server, 0, SRTO_TSBPDMODE, &yes, sizeof(yes));

        auto addr = CreateAddrInet("0.0.0.0", 1234);
        srt_bind(server, (sockaddr*)&addr, sizeof(addr));
        srt_listen(server, 1);

        sockaddr_in remote;
        int len;
        auto server_sock = srt_accept(server, (sockaddr*)&remote, &len);
        if (server_sock == -1) {
            std::cout << srt_getlasterror_str() << std::endl;
        }

        std::cout << "Connection initialized" << std::endl;

        int i = 0;
        for (;;) {
            char buf[4096];
            SRT_MSGCTRL c;
            auto read = srt_recvmsg2(server_sock, buf, 4096, &c);
            if (read == -1) {
                std::cout << srt_getlasterror_str() << std::endl;
                srt_clearlasterror();
                continue;
            }
            if (i != atol(buf)) {
                std::cout << "Got behind! " << i << std::endl;
                i = atol(buf) + 1;
            }
            else {
                ++i;
            }
        }
    });

    auto client = std::thread([] {

        int client = srt_create_socket();

        const int yes = 1;
        srt_setsockopt(client, 0, SRTO_TSBPDMODE, &yes, sizeof(yes));
        srt_setsockopt(client, 0, SRTO_SENDER, &yes, sizeof(yes));

        auto addr = CreateAddrInet("127.0.0.1", 1234);
        srt_connect(client, (sockaddr*)&addr, sizeof(addr));

        int i = 0;
        for (;;) {
            std::string buffer = std::to_string(i++);
            buffer.push_back('\0');
            for (int a = 0; buffer.size() != 1316; ++a) {
                buffer.push_back(rand());
            }

            SRT_MSGCTRL c = { 0 };
            srt_sendmsg2(client, buffer.data(), buffer.length(), &c);

            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }

    });

    server.join();
    client.join();

}