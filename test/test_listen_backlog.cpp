/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Written by:
 *             Haivision Systems Inc.
 */

#include <gtest/gtest.h>
#include <future>
#include <thread>

#include "srt.h"

using namespace std;


class TestListenBacklog
	: public ::testing::Test
{
protected:
	TestListenBacklog()
	{
		// initialization code here
	}

	~TestListenBacklog()
	{
		// cleanup any pending stuff, but no exceptions allowed
	}

protected:
	// SetUp() is run immediately before a test starts.
	void SetUp()
	{
		ASSERT_EQ(srt_startup(), 0);
		const int yes = 1;

		for (size_t i = 0; i < max_callers; ++i)
		{
			m_caller_sock[i] = srt_create_socket();
			ASSERT_NE(m_caller_sock[i], SRT_INVALID_SOCK);
			ASSERT_EQ(srt_setsockopt(m_caller_sock[i], 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
			ASSERT_EQ(srt_setsockopt(m_caller_sock[i], 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
		}

		m_listen_sock = srt_create_socket();
		ASSERT_NE(m_listen_sock, SRT_INVALID_SOCK);
		ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
		ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
	}

	void TearDown()
	{
		// Code here will be called just after the test completes.
		// OK to throw exceptions from here if needed.
		for (size_t i = 0; i < max_callers; ++i)
		{
			if (m_caller_sock[i] == SRT_INVALID_SOCK)
				continue;
			ASSERT_NE(srt_close(m_caller_sock[i]), SRT_ERROR);
		}
		ASSERT_NE(srt_close(m_listen_sock), SRT_ERROR);
		srt_cleanup();
	}

public:


	// Passing argument by ref because ASSERT_* returns void
	void create(SRTSOCKET &sock)
	{
		sock = srt_create_socket();
		ASSERT_NE(sock, SRT_INVALID_SOCK);
		const int yes = 1;
		ASSERT_EQ(srt_setsockopt(sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
		ASSERT_EQ(srt_setsockopt(sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
	}

protected:
	// put in any custom data members that you need

	static const size_t max_callers = 5;
	SRTSOCKET m_caller_sock[max_callers] = { SRT_INVALID_SOCK };
	SRTSOCKET m_listen_sock = SRT_INVALID_SOCK;

	int       m_pollid = 0;
};



TEST_F(TestListenBacklog, BacklogOne)
{
	const int backlog = 1;

	// Specify address
	sockaddr_in sa;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(5200);
	ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
	sockaddr* psa = (sockaddr*)&sa;
	ASSERT_NE(srt_bind(m_listen_sock, psa, sizeof sa), SRT_ERROR);

	srt_listen(m_listen_sock, backlog);

	auto accept_async = [](SRTSOCKET listen_sock) {
		sockaddr_in client_address;
		int length = sizeof(sockaddr_in);
		const SRTSOCKET accepted_socket = srt_accept(listen_sock, (sockaddr*)&client_address, &length);
		return accepted_socket;
	};
	auto accept_res = async(launch::async, accept_async, m_listen_sock);

	int connect_res = srt_connect(m_caller_sock[0], psa, sizeof sa);
	EXPECT_EQ(connect_res, SRT_SUCCESS) << "First connection atempt should suceed";

	const SRTSOCKET accepted_sock = accept_res.get();
	ASSERT_NE(accepted_sock, SRT_INVALID_SOCK);

	connect_res = srt_connect(m_caller_sock[1], psa, sizeof sa);
	EXPECT_EQ(connect_res, SRT_EUNKNOWN) << "There should aready be one connection, so failer is expected";

	ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);

	connect_res = srt_connect(m_caller_sock[2], psa, sizeof sa);
	EXPECT_EQ(connect_res, SRT_SUCCESS) << "Accepted socket is disconnected. A new connection should succeed";

	// Check backlog.
}



