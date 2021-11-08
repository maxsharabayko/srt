/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 * Based on the proposal by Russell Greene (Issue #440)
 *
 */

#include <gtest/gtest.h>

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"

#include <array>
#include <thread>
#include <fstream>
#include <ctime>
#include <vector>

//#pragma comment (lib, "ws2_32.lib")

TEST(Transmission, FileUpload)
{
    srt_startup();

    // Generate the source file
    // We need a file that will contain more data
    // than can be contained in one sender buffer.

    SRTSOCKET sock_lsn = srt_create_socket(), sock_clr = srt_create_socket();

    const int tt = SRTT_FILE;
    srt_setsockflag(sock_lsn, SRTO_TRANSTYPE, &tt, sizeof tt);
    srt_setsockflag(sock_clr, SRTO_TRANSTYPE, &tt, sizeof tt);

    // Configure listener 
    sockaddr_in sa_lsn = sockaddr_in();
    sa_lsn.sin_family = AF_INET;
    sa_lsn.sin_addr.s_addr = INADDR_ANY;
    sa_lsn.sin_port = htons(5555);

    // Find unused a port not used by any other service.    
    // Otherwise srt_connect may actually connect.
    int bind_res = -1;
    for (int port = 5000; port <= 5555; ++port)
    {
        sa_lsn.sin_port = htons(port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);
        if (bind_res == 0)
        {
            std::cout << "Running test on port " << port << "\n";
            break;
        }

        ASSERT_TRUE(bind_res == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_res;
    }

    ASSERT_GE(bind_res, 0);

    srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);

    int optval = 0;
    int optlen = sizeof optval;
    ASSERT_EQ(srt_getsockflag(sock_lsn, SRTO_SNDBUF, &optval, &optlen), 0);
    const size_t filesize = 7 * optval;

    {
        std::cout << "WILL CREATE source file with size=" << filesize << " (= 7 * " << optval << "[sndbuf])\n";
        std::ofstream outfile("file.source", std::ios::out | std::ios::binary);
        ASSERT_EQ(!!outfile, true) << srt_getlasterror_str();

        srand(time(0));

        for (size_t i = 0; i < filesize; ++i)
        {
            char outbyte = rand() % 255;
            outfile.write(&outbyte, 1);
        }
    }

    srt_listen(sock_lsn, 1);

    // Start listener-receiver thread

    bool thread_exit = false;

    auto client = std::thread([&]
    {
        sockaddr_in remote;
        int len = sizeof remote;
        const SRTSOCKET accepted_sock = srt_accept(sock_lsn, (sockaddr*)&remote, &len);
        ASSERT_GT(accepted_sock, 0);

        if (accepted_sock == SRT_INVALID_SOCK)
        {
            std::cerr << srt_getlasterror_str() << std::endl;
            EXPECT_NE(srt_close(sock_lsn), SRT_ERROR);
            return;
        }

        std::ofstream copyfile("file.target", std::ios::out | std::ios::trunc | std::ios::binary);

        std::vector<char> buf(1456);

        for (;;)
        {
            int n = srt_recv(accepted_sock, buf.data(), 1456);
            ASSERT_NE(n, SRT_ERROR);
            if (n == 0)
            {
                std::cerr << "Received 0 bytes, breaking.\n";
                break;
            }

            // Write to file any amount of data received
            copyfile.write(buf.data(), n);
        }

        EXPECT_NE(srt_close(accepted_sock), SRT_ERROR);

        thread_exit = true;
    });

    sockaddr_in sa = sockaddr_in();
    sa.sin_family = AF_INET;
    sa.sin_port = sa_lsn.sin_port;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_connect(sock_clr, (sockaddr*)&sa, sizeof(sa));

    std::cout << "Connection initialized" << std::endl;

    std::ifstream ifile("file.source", std::ios::in | std::ios::binary);
    std::vector<char> buf(1456);

    for (;;)
    {
        size_t n = ifile.read(buf.data(), 1456).gcount();
        size_t shift = 0;
        while (n > 0)
        {
            const int st = srt_send(sock_clr, buf.data()+shift, n);
            ASSERT_GT(st, 0) << srt_getlasterror_str();

            n -= st;
            shift += st;
        }

        if (ifile.eof())
        {
            break;
        }

        ASSERT_EQ(ifile.good(), true);
    }

    // Finished sending, close the socket
    std::cout << "Finished sending, closing sockets:\n";
    srt_close(sock_clr);
    srt_close(sock_lsn);

    std::cout << "Sockets closed, joining receiver thread\n";
    client.join();

    std::ifstream tarfile("file.target");
    EXPECT_EQ(!!tarfile, true);

    tarfile.seekg(0, std::ios::end);
    size_t tar_size = tarfile.tellg();
    EXPECT_EQ(tar_size, filesize);

    std::cout << "Comparing files\n";
    // Compare files
    tarfile.seekg(0, std::ios::end);
    ifile.seekg(0, std::ios::beg);

    for (size_t i = 0; i < tar_size; ++i)
    {
        EXPECT_EQ(ifile.get(), tarfile.get());
    }

    EXPECT_EQ(ifile.get(), EOF);
    EXPECT_EQ(tarfile.get(), EOF);

    remove("file.source");
    remove("file.target");

    (void)srt_cleanup();
}

TEST(Transmission, FileUploadNonblock)
{
    srt_startup();

    // Generate the source file
    // We need a file that will contain more data
    // than can be contained in one sender buffer.

    SRTSOCKET sock_lsn = srt_create_socket(), sock_clr = srt_create_socket();

    const int tt = SRTT_FILE;
    srt_setsockflag(sock_lsn, SRTO_TRANSTYPE, &tt, sizeof tt);
    srt_setsockflag(sock_clr, SRTO_TRANSTYPE, &tt, sizeof tt);

    // Configure listener 
    sockaddr_in sa_lsn = sockaddr_in();
    sa_lsn.sin_family = AF_INET;
    sa_lsn.sin_addr.s_addr = INADDR_ANY;
    sa_lsn.sin_port = htons(5555);

    // Find unused a port not used by any other service.    
    // Otherwise srt_connect may actually connect.
    int bind_res = -1;
    for (int port = 5000; port <= 5555; ++port)
    {
        sa_lsn.sin_port = htons(port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);
        if (bind_res == 0)
        {
            std::cout << "Running test on port " << port << "\n";
            break;
        }

        ASSERT_TRUE(bind_res == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_res;
    }

    ASSERT_GE(bind_res, 0);

    srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);

    int optval = 0;
    int optlen = sizeof optval;
    ASSERT_EQ(srt_getsockflag(sock_lsn, SRTO_SNDBUF, &optval, &optlen), 0);
    const size_t filesize = 7 * optval;

    {
        std::cout << "WILL CREATE source file with size=" << filesize << " (= 7 * " << optval << "[sndbuf])\n";
        std::ofstream outfile("file.source", std::ios::out | std::ios::binary);
        ASSERT_EQ(!!outfile, true) << srt_getlasterror_str();

        srand(time(0));

        for (size_t i = 0; i < filesize; ++i)
        {
            char outbyte = rand() % 255;
            outfile.write(&outbyte, 1);
        }
    }

    srt_listen(sock_lsn, 1);

    // Start listener-receiver thread

    bool thread_exit = false;

    auto client = std::thread([&]
        {
            sockaddr_in remote;
            int len = sizeof remote;
            const SRTSOCKET accepted_sock = srt_accept(sock_lsn, (sockaddr*)&remote, &len);
            ASSERT_GT(accepted_sock, 0);

            if (accepted_sock == SRT_INVALID_SOCK)
            {
                std::cerr << srt_getlasterror_str() << std::endl;
                EXPECT_NE(srt_close(sock_lsn), SRT_ERROR);
                return;
            }

            const int no = 0;
            srt_setsockflag(accepted_sock, SRTO_RCVSYN, &no, sizeof no);
            srt_setsockflag(accepted_sock, SRTO_SNDSYN, &no, sizeof no);

            const int pollid = srt_epoll_create();
            ASSERT_GE(pollid, 0);
            const int epoll_etin = SRT_EPOLL_IN | SRT_EPOLL_ET;
            ASSERT_NE(srt_epoll_add_usock(pollid, accepted_sock, &epoll_etin), SRT_ERROR);

            std::ofstream copyfile("file.target", std::ios::out | std::ios::trunc | std::ios::binary);

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
                    const int n = srt_recv(accepted_sock, buf.data(), buf.size());

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
                    copyfile.write(buf.data(), n);
                    ++num_reads;
                } while (true);
            }

            EXPECT_NE(srt_close(accepted_sock), SRT_ERROR);

            thread_exit = true;
        });

    sockaddr_in sa = sockaddr_in();
    sa.sin_family = AF_INET;
    sa.sin_port = sa_lsn.sin_port;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_connect(sock_clr, (sockaddr*)&sa, sizeof(sa));

    std::cout << "Connection initialized" << std::endl;

    std::ifstream ifile("file.source", std::ios::in | std::ios::binary);
    std::vector<char> buf(1456);

    for (;;)
    {
        size_t n = ifile.read(buf.data(), 1456).gcount();
        size_t shift = 0;
        while (n > 0)
        {
            const int st = srt_send(sock_clr, buf.data() + shift, n);
            ASSERT_GT(st, 0) << srt_getlasterror_str();

            n -= st;
            shift += st;
        }

        if (ifile.eof())
        {
            break;
        }

        ASSERT_EQ(ifile.good(), true);
    }

    // Finished sending, close the socket
    std::cout << "Finished sending, closing sockets:\n";
    srt_close(sock_clr);
    srt_close(sock_lsn);

    std::cout << "Sockets closed, joining receiver thread\n";
    client.join();

    std::ifstream tarfile("file.target");
    EXPECT_EQ(!!tarfile, true);

    tarfile.seekg(0, std::ios::end);
    size_t tar_size = tarfile.tellg();
    EXPECT_EQ(tar_size, filesize) << "sent " << filesize << ", received " << tar_size << " bytes";

    std::cout << "Comparing files\n";
    // Compare files
    tarfile.seekg(0, std::ios::end);
    ifile.seekg(0, std::ios::beg);

    for (size_t i = 0; i < tar_size; ++i)
    {
        EXPECT_EQ(ifile.get(), tarfile.get());
    }

    EXPECT_EQ(ifile.get(), EOF);
    EXPECT_EQ(tarfile.get(), EOF);

    remove("file.source");
    remove("file.target");

    (void)srt_cleanup();
}


