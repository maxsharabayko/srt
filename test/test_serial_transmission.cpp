/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 * Submited by: Russell Greene (Issue #440)
 *
 */

#include <gtest/gtest.h>

#ifdef _WIN32
#define _WINSOCKAPI_ // to include Winsock2.h instead of Winsock.h from windows.h
#include <winsock2.h>

//#define _WINSOCK_DEPRECATED_NO_WARNINGS
#if defined(__GNUC__) || defined(__MINGW32__)
extern "C" {
    WINSOCK_API_LINKAGE  INT WSAAPI inet_pton(INT Family, PCSTR pszAddrString, PVOID pAddrBuf);
    WINSOCK_API_LINKAGE  PCSTR WSAAPI inet_ntop(INT  Family, PVOID pAddr, PSTR pStringBuf, size_t StringBufSize);
}
#endif

#define INC__WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"

#include <thread>

//#pragma comment (lib, "ws2_32.lib")

TEST(Transmission, Serial)
{
    srt_startup();
    //srt_setloglevel(SRT_LOG_LEVEL_MAX);

    auto client = std::thread([]
    {
        int snd_listener = srt_create_socket();

        const int yes = 1;
        const int no  = 0;
        EXPECT_EQ(srt_setsockopt(snd_listener, 0, SRTO_TSBPDMODE, &no, sizeof(no)), SRT_SUCCESS);
        EXPECT_EQ(srt_setsockopt(snd_listener, 0, SRTO_SENDER,    &yes, sizeof(yes)), SRT_SUCCESS);

        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(5555);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

        srt_bind  (snd_listener, (sockaddr*)&sa, sizeof(sa));
        srt_listen(snd_listener, 1);

        sockaddr_in remote;
        int len;
        const SRTSOCKET accepted_sock = srt_accept(snd_listener, (sockaddr*)&remote, &len);
        EXPECT_GT(accepted_sock, 0);

        if (accepted_sock == SRT_INVALID_SOCK) {
            std::cerr << srt_getlasterror_str() << std::endl;
            EXPECT_NE(srt_close(snd_listener), SRT_ERROR);
            return;
        }

        for (int i = 0; i < 5000; ++i)
        {
            std::string buffer = std::to_string(i);
            buffer.push_back('\0');
            for (int a = 0; buffer.size() != 1316; ++a) {
                buffer.push_back(rand());
            }

            SRT_MSGCTRL c = { 0 };
            srt_sendmsg2(accepted_sock, buffer.data(), buffer.length(), &c);

            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }

        EXPECT_NE(srt_close(snd_listener), SRT_ERROR);
    });

    int server = srt_create_socket();

    const int yes = 1;
    const int no = 0;
    srt_setsockopt(server, 0, SRTO_TSBPDMODE, &no, sizeof(no));

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_connect(server, (sockaddr*)&sa, sizeof(sa));

    std::cout << "Connection initialized" << std::endl;

    for (int i = 0; i < 5000; ++i)
    {
        char buf[4096];
        SRT_MSGCTRL c;
        auto read = srt_recvmsg2(server, buf, sizeof buf, &c);
        if (read == -1) {
            std::cerr << srt_getlasterror_str() << std::endl;
            srt_clearlasterror();
            continue;
        }
        const long val = atol(buf);
        if (i != atol(buf)) {
            std::cout << "FAIL: Expected: " << i << " received " << val << std::endl;
            i = val;
        }
        //else {
        //    std::cerr << "OK: Expected: " << i << " received " << val << std::endl;
        //}
    }

    //std::cout << "Receiver: left the loop" << std::endl;

    client.join();

    std::cout << "Receiver: after join" << std::endl;

    (void)srt_cleanup();
}