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
#include <array>
#include <future>
#include <thread>
#include <condition_variable> 
#include <mutex>

#include "srt.h"
#include "sync.h"


namespace ciphermode {

enum PEER_TYPE
{
    PEER_CALLER = 0,
    PEER_LISTENER = 1,
    PEER_COUNT = 2,  // Number of peers
};


enum CHECK_SOCKET_TYPE
{
    CHECK_SOCKET_CALLER = 0,
    CHECK_SOCKET_ACCEPTED = 1,
    CHECK_SOCKET_COUNT = 2,  // Number of peers
};


enum TEST_CASE
{
    TEST_CASE_A_1 = 0,
    TEST_CASE_A_2,
    TEST_CASE_A_3,
    TEST_CASE_A_4,
    TEST_CASE_A_5,
    TEST_CASE_B_1,
    TEST_CASE_B_2,
    TEST_CASE_B_3,
    TEST_CASE_B_4,
    TEST_CASE_B_5,
    TEST_CASE_C_1,
    TEST_CASE_C_2,
    TEST_CASE_C_3,
    TEST_CASE_C_4,
    TEST_CASE_C_5,
    TEST_CASE_D_1,
    TEST_CASE_D_2,
    TEST_CASE_D_3,
    TEST_CASE_D_4,
    TEST_CASE_D_5,
};


struct TestResultNonBlocking
{
    int     connect_ret;
    int     accept_ret;
    int     epoll_wait_ret;
    int     epoll_event;
    int     socket_state[CHECK_SOCKET_COUNT];
    int     km_state[CHECK_SOCKET_COUNT];
};


struct TestResultBlocking
{
    int     connect_ret;
    int     accept_ret;
    int     socket_state[CHECK_SOCKET_COUNT];
    int     km_state[CHECK_SOCKET_COUNT];
};


template<typename TResult>
struct TestCase
{
    int                ciphermode[PEER_COUNT]; // 0 - AES-CTR, 1 - AES-GCM.
    const std::string(&password)[PEER_COUNT];
    TResult            expected_result;
};

typedef TestCase<TestResultNonBlocking>  TestCaseNonBlocking;
typedef TestCase<TestResultBlocking>     TestCaseBlocking;



static const std::string s_pwd_a("s!t@r#i$c^t");
static const std::string s_pwd_b("s!t@r#i$c^tu");
static const std::string s_pwd_no("");


const int IGNORE_EPOLL = -2;
const int IGNORE_SRTS = -1;

const TestCaseNonBlocking g_test_matrix_non_blocking[] =
{
    // CIPHERMODE        |  Password           |                                | EPoll wait                       | socket_state                            |  KM State
    // caller | listener |  caller  | listener |  connect_ret   accept_ret      |  ret         | event             | caller              accepted |  caller              listener
    /*A.1 */ { {2,     2 }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
    /*A.2 */ { {2,     2 }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*A.3 */ { {2,     2 }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*A.4 */ { {2,     2 }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*A.5 */ { {2,     2 }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

    /*B.1 */ { {2,     1 }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS, SRT_INVALID_SOCK,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
    /*B.2 */ { {2,     1 }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_UNSECURED, SRT_KM_S_BADSECRET}}},
    /*B.3 */ { {2,     1 }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
    /*B.4 */ { {2,     1 }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_UNSECURED,  SRT_KM_S_NOSECRET}}},
    /*B.5 */ { {2,     1 }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

    /*C.1 */ { {1,     2 }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*C.2 */ { {1,     2 }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*C.3 */ { {1,     2 }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*C.4 */ { {1,     2 }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
    /*C.5 */ { {1,     2 }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}}
};


const TestCaseBlocking g_test_matrix_blocking[] =
{
    // ENFORCEDENC       |  Password           |                      -2: don't check | socket_state                   |  KM State
    // caller | listener |  caller  | listener |  connect_ret         accept_ret      | caller                accepted |  caller              listener
    /*A.1 */ { {2,    2  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
    /*A.2 */ { {2,    2  }, {s_pwd_a,   s_pwd_b}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*A.3 */ { {2,    2  }, {s_pwd_a,  s_pwd_no}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*A.4 */ { {2,    2  }, {s_pwd_no,  s_pwd_b}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*A.5 */ { {2,    2  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

    /*B.1 */ { {2,    1  }, {s_pwd_a,   s_pwd_a}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
    /*B.2 */ { {2,    1  }, {s_pwd_a,   s_pwd_b}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
    /*B.3 */ { {2,    1  }, {s_pwd_a,  s_pwd_no}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
    /*B.4 */ { {2,    1  }, {s_pwd_no,  s_pwd_b}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,  SRT_KM_S_NOSECRET}}},
    /*B.5 */ { {2,    1  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

    /*C.1 */ { {1,    2  }, {s_pwd_a,   s_pwd_a}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*C.2 */ { {1,    2  }, {s_pwd_a,   s_pwd_b}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*C.3 */ { {1,    2  }, {s_pwd_a,  s_pwd_no}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*C.4 */ { {1,    2  }, {s_pwd_no,  s_pwd_b}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
    /*C.5 */ { {1,    2  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}}
};

} // namespace ciphermode

using namespace ciphermode;

class TestCipherMode
    : public ::testing::Test
{
protected:
    TestCipherMode()
        : m_caller_done(false)
    {
        // initialization code here
    }

    ~TestCipherMode()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // SetUp() is run immediately before a test starts.
    void SetUp()
    {
        ASSERT_EQ(srt_startup(), 0);

        srt_addlogfa(SRT_LOGFA_HAICRYPT);
        srt_setloglevel(srt_logging::LogLevel::note);

        m_pollid = srt_epoll_create();
        ASSERT_GE(m_pollid, 0);

        m_caller_socket = srt_create_socket();
        ASSERT_NE(m_caller_socket, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(m_caller_socket, SRTO_SENDER, &s_yes, sizeof s_yes), SRT_ERROR);
        ASSERT_NE(srt_setsockopt(m_caller_socket, 0, SRTO_TSBPDMODE, &s_yes, sizeof s_yes), SRT_ERROR);

        m_listener_socket = srt_create_socket();
        ASSERT_NE(m_listener_socket, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(m_listener_socket, SRTO_SENDER, &s_no, sizeof s_no), SRT_ERROR);
        ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_TSBPDMODE, &s_yes, sizeof s_yes), SRT_ERROR);

        // Will use this epoll to wait for srt_accept(...)
        const int epoll_out = SRT_EPOLL_IN | SRT_EPOLL_ERR;
        ASSERT_NE(srt_epoll_add_usock(m_pollid, m_listener_socket, &epoll_out), SRT_ERROR);
    }

    void TearDown()
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        ASSERT_NE(srt_close(m_caller_socket), SRT_ERROR);
        ASSERT_NE(srt_close(m_listener_socket), SRT_ERROR);
        srt_cleanup();
    }


public:
    int SetCryptoMode(PEER_TYPE peer, int32_t value)
    {
        const SRTSOCKET& socket = peer == PEER_CALLER ? m_caller_socket : m_listener_socket;
        return srt_setsockopt(socket, 0, SRTO_CRYPTOMODE, &value, sizeof value);
    }


    int32_t GetCryptoMode(PEER_TYPE peer_type)
    {
        const SRTSOCKET socket = peer_type == PEER_CALLER ? m_caller_socket : m_listener_socket;
        int32_t optval;
        int  optlen = sizeof optval;
        EXPECT_EQ(srt_getsockopt(socket, 0, SRTO_CRYPTOMODE, (void*)&optval, &optlen), SRT_SUCCESS);
        return optval;
    }


    int SetPassword(PEER_TYPE peer_type, const std::basic_string<char>& pwd)
    {
        const SRTSOCKET socket = peer_type == PEER_CALLER ? m_caller_socket : m_listener_socket;
        return srt_setsockopt(socket, 0, SRTO_PASSPHRASE, pwd.c_str(), (int)pwd.size());
    }


    int GetKMState(SRTSOCKET socket)
    {
        int km_state = 0;
        int opt_size = sizeof km_state;
        EXPECT_EQ(srt_getsockopt(socket, 0, SRTO_KMSTATE, reinterpret_cast<void*>(&km_state), &opt_size), SRT_SUCCESS);

        return km_state;
    }


    int GetSocetkOption(SRTSOCKET socket, SRT_SOCKOPT opt)
    {
        int val = 0;
        int size = sizeof val;
        EXPECT_EQ(srt_getsockopt(socket, 0, opt, reinterpret_cast<void*>(&val), &size), SRT_SUCCESS);

        return val;
    }

    template<typename TResult>
    SRTSOCKET Accept(const TResult& expect)
    {
        const int epoll_event = WaitOnEpoll(expect);

        // In a blocking mode we expect a socket returned from srt_accept() if the srt_connect succeeded.
        // In a non-blocking mode we expect a socket returned from srt_accept() if the srt_connect succeeded,
        // otherwise SRT_INVALID_SOCKET after the listening socket is closed.
        sockaddr_in client_address;
        int length = sizeof(sockaddr_in);
        SRTSOCKET accepted_socket = SRT_INVALID_SOCK;
        if (epoll_event == SRT_EPOLL_IN)
        {
            accepted_socket = srt_accept(m_listener_socket, (sockaddr*)&client_address, &length);
            std::cout << "ACCEPT: done, result=" << accepted_socket << std::endl;
        }
        else
        {
            std::cout << "ACCEPT: NOT done\n";
        }

        if (accepted_socket == SRT_INVALID_SOCK)
        {
            std::cerr << "[T] ACCEPT ERROR: " << srt_getlasterror_str() << std::endl;
        }
        else
        {
            std::cerr << "[T] ACCEPT SUCCEEDED: @" << accepted_socket << "\n";
        }

        EXPECT_NE(accepted_socket, 0);
        if (expect.accept_ret == SRT_INVALID_SOCK)
        {
            EXPECT_EQ(accepted_socket, SRT_INVALID_SOCK);
        }
        else if (expect.accept_ret != -2)
        {
            EXPECT_NE(accepted_socket, SRT_INVALID_SOCK);
        }

        if (accepted_socket != SRT_INVALID_SOCK && expect.socket_state[CHECK_SOCKET_ACCEPTED] != IGNORE_SRTS)
        {
            if (m_is_tracing)
            {
                std::cerr << "EARLY Socket state accepted: " << m_socket_state[srt_getsockstate(accepted_socket)]
                    << " (expected: " << m_socket_state[expect.socket_state[CHECK_SOCKET_ACCEPTED]] << ")\n";
                std::cerr << "KM State accepted:     " << m_km_state[GetKMState(accepted_socket)] << '\n';
                std::cerr << "RCV KM State accepted:     " << m_km_state[GetSocetkOption(accepted_socket, SRTO_RCVKMSTATE)] << '\n';
                std::cerr << "SND KM State accepted:     " << m_km_state[GetSocetkOption(accepted_socket, SRTO_SNDKMSTATE)] << '\n';
            }

            // We have to wait some time for the socket to be able to process the HS response from the caller.
            // In test cases B2 - B4 the socket is expected to change its state from CONNECTED to BROKEN
            // due to KM mismatches
            do
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } while (!m_caller_done);

            // Special case when the expected state is "broken": if so, tolerate every possible
            // socket state, just NOT LESS than SRTS_BROKEN, and also don't read any flags on that socket.

            if (expect.socket_state[CHECK_SOCKET_ACCEPTED] == SRTS_BROKEN)
            {
                EXPECT_GE(srt_getsockstate(accepted_socket), SRTS_BROKEN);
            }
            else
            {
                EXPECT_EQ(srt_getsockstate(accepted_socket), expect.socket_state[CHECK_SOCKET_ACCEPTED]);
                EXPECT_EQ(GetSocetkOption(accepted_socket, SRTO_SNDKMSTATE), expect.km_state[CHECK_SOCKET_ACCEPTED]);
            }

            if (m_is_tracing)
            {
                const SRT_SOCKSTATUS status = srt_getsockstate(accepted_socket);
                std::cerr << "LATE Socket state accepted: " << m_socket_state[status]
                    << " (expected: " << m_socket_state[expect.socket_state[CHECK_SOCKET_ACCEPTED]] << ")\n";
            }
        }

        return accepted_socket;
    }


    template<typename TResult>
    int WaitOnEpoll(const TResult& expect);


    template<typename TResult>
    const TestCase<TResult>& GetTestMatrix(TEST_CASE test_case) const;

    template<typename TResult>
    void TestConnect(TEST_CASE test_case/*, bool is_blocking*/)
    {
        const bool is_blocking = std::is_same<TResult, TestResultBlocking>::value;
        if (is_blocking)
        {
            ASSERT_NE(srt_setsockopt(m_caller_socket, 0, SRTO_RCVSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            ASSERT_NE(srt_setsockopt(m_caller_socket, 0, SRTO_SNDSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_RCVSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_SNDSYN, &s_yes, sizeof s_yes), SRT_ERROR);
        }
        else
        {
            ASSERT_NE(srt_setsockopt(m_caller_socket, 0, SRTO_RCVSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
            ASSERT_NE(srt_setsockopt(m_caller_socket, 0, SRTO_SNDSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_RCVSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_SNDSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
        }

        // Prepare input state
        const TestCase<TResult>& test = GetTestMatrix<TResult>(test_case);
        ASSERT_EQ(SetCryptoMode(PEER_CALLER, test.ciphermode[PEER_CALLER]), SRT_SUCCESS);
        ASSERT_EQ(SetCryptoMode(PEER_LISTENER, test.ciphermode[PEER_LISTENER]), SRT_SUCCESS);

        ASSERT_EQ(SetPassword(PEER_CALLER, test.password[PEER_CALLER]), SRT_SUCCESS);
        ASSERT_EQ(SetPassword(PEER_LISTENER, test.password[PEER_LISTENER]), SRT_SUCCESS);

        const TResult& expect = test.expected_result;

        // Start testing
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(5200);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
        sockaddr* psa = (sockaddr*)&sa;
        ASSERT_NE(srt_bind(m_listener_socket, psa, sizeof sa), SRT_ERROR);
        ASSERT_NE(srt_listen(m_listener_socket, 4), SRT_ERROR);

        auto accepted_future = std::async(std::launch::async, &TestCipherMode::Accept<TResult>, this, expect);

        const int connect_ret = srt_connect(m_caller_socket, psa, sizeof sa);
        EXPECT_EQ(connect_ret, expect.connect_ret);

        if (connect_ret == SRT_ERROR && connect_ret != expect.connect_ret)
        {
            const int errc = srt_getlasterror(NULL);
            std::cerr << "UNEXPECTED! srt_connect returned error: "
                << srt_getlasterror_str() << " (code " << errc
                << ", reject reason " << ((errc == SRT_ECONNREJ) ? srt_rejectreason_str(srt_getrejectreason(m_caller_socket)) : "n/a")
                << ").\n";
        }

        m_caller_done = true;

        if (is_blocking == false)
            accepted_future.wait();

        if (m_is_tracing)
        {
            std::cerr << "Socket state caller:   " << m_socket_state[srt_getsockstate(m_caller_socket)] << "\n";
            std::cerr << "Socket state listener: " << m_socket_state[srt_getsockstate(m_listener_socket)] << "\n";
            std::cerr << "KM State caller:       " << m_km_state[GetKMState(m_caller_socket)] << '\n';
            std::cerr << "RCV KM State caller:   " << m_km_state[GetSocetkOption(m_caller_socket, SRTO_RCVKMSTATE)] << '\n';
            std::cerr << "SND KM State caller:   " << m_km_state[GetSocetkOption(m_caller_socket, SRTO_SNDKMSTATE)] << '\n';
            std::cerr << "KM State listener:     " << m_km_state[GetKMState(m_listener_socket)] << '\n';
            const int errc = srt_getlasterror(NULL);
            if (connect_ret == SRT_ERROR && errc == SRT_ECONNREJ)
            {
                std::cerr << "Caller reject reason: " << srt_rejectreason_str(srt_getrejectreason(m_caller_socket)) << "\n";
            }
        }


        // If a blocking call to srt_connect() returned error, then the state is not valid,
        // but we still check it because we know what it should be. This way we may see potential changes in the core behavior.
        if (is_blocking)
        {
            EXPECT_EQ(srt_getsockstate(m_caller_socket), expect.socket_state[CHECK_SOCKET_CALLER]);
        }
        // A caller socket, regardless of the mode, if it's not expected to be connected, check negatively.
        std::cerr << "Caller: checking state\n";
        if (expect.socket_state[CHECK_SOCKET_CALLER] == SRTS_CONNECTED)
        {
            EXPECT_EQ(srt_getsockstate(m_caller_socket), SRTS_CONNECTED);

            const SRTSOCKET accepted_socket = accepted_future.get();
            EXPECT_NE(accepted_socket, SRT_INVALID_SOCK);
            // Make accepted socket blocking to avoid using epoll for IO.
            const bool is_blocking = std::is_same<TResult, TestResultBlocking>::value;
            if (!is_blocking)
            {
                EXPECT_NE(srt_setsockopt(accepted_socket, 0, SRTO_RCVSYN, &s_yes, sizeof s_yes), SRT_ERROR);
                EXPECT_NE(srt_setsockopt(accepted_socket, 0, SRTO_SNDSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            }

            auto accept_io = std::async(std::launch::async, [&](SRTSOCKET accepted_socket) {
                std::cerr << "Accepted socket: waiting for RCV\n";
                std::array<char, 1500> buff;
                const int num_bytes = srt_recvmsg(accepted_socket, buff.data(), (int)buff.size());
                EXPECT_NE(num_bytes, SRT_ERROR);
                EXPECT_NE(srt_sendmsg2(accepted_socket, buff.data(), num_bytes, nullptr), SRT_ERROR);
            }, accepted_socket);

            std::array<char, 1316> buff;
            EXPECT_NE(srt_sendmsg2(m_caller_socket, buff.data(), (int) buff.size(), nullptr), SRT_ERROR);

            std::cerr << "Caller: sent message, waiting for RCV\n";
            if (!is_blocking)
            {
                const int io_epoll = srt_epoll_create();
                EXPECT_GE(io_epoll, 0);

                const int epoll_f_all = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                EXPECT_NE(srt_epoll_add_usock(io_epoll, m_caller_socket, &epoll_f_all), SRT_ERROR);

                const int default_len = 3;
                SRT_EPOLL_EVENT ready[default_len];
                const int epoll_res = srt_epoll_uwait(io_epoll, ready, default_len, 500);

                // TODO: check if the socket read-readiness was signalled.
            }

            const int num_bytes = srt_recvmsg(m_caller_socket, buff.data(), (int) buff.size());
            EXPECT_NE(num_bytes, SRT_ERROR);
            EXPECT_EQ(num_bytes, buff.size());

            accept_io.wait();
        }
        else
        {
            // If the socket is not expected to be connected (might be CONNECTING),
            // then it is ok if it's CONNECTING or BROKEN.
            EXPECT_NE(srt_getsockstate(m_caller_socket), SRTS_CONNECTED);
        }

        std::cerr << "Caller: checking state done\n";

        EXPECT_EQ(GetSocetkOption(m_caller_socket, SRTO_RCVKMSTATE), expect.km_state[CHECK_SOCKET_CALLER]);

        EXPECT_EQ(srt_getsockstate(m_listener_socket), SRTS_LISTENING);
        EXPECT_EQ(GetKMState(m_listener_socket), SRT_KM_S_UNSECURED);

        if (is_blocking)
        {
            // srt_accept() has no timeout, so we have to close the socket and wait for the thread to exit.
            // Just give it some time and close the socket.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::cerr << "closing listener\n";
            EXPECT_NE(srt_close(m_listener_socket), SRT_ERROR);
            std::cerr << "Waiting accept thread.\n";
            if (accepted_future.valid())
                accepted_future.wait();
        }
    }


private:
    // put in any custom data members that you need

    SRTSOCKET m_caller_socket = SRT_INVALID_SOCK;
    SRTSOCKET m_listener_socket = SRT_INVALID_SOCK;

    int       m_pollid = 0;

    const bool s_yes = true;
    const bool s_no = false;

    srt::sync::atomic<bool> m_caller_done;
    const bool          m_is_tracing = true;
    static const char* m_km_state[];
    static const char* const* m_socket_state;
};



template<>
int TestCipherMode::WaitOnEpoll<TestResultBlocking>(const TestResultBlocking&)
{
    return SRT_EPOLL_IN;
}

static std::ostream& PrintEpollEvent(std::ostream& os, int events, int et_events)
{
    using namespace std;

    static pair<int, const char*> const namemap[] = {
        make_pair(SRT_EPOLL_IN, "R"),
        make_pair(SRT_EPOLL_OUT, "W"),
        make_pair(SRT_EPOLL_ERR, "E"),
        make_pair(SRT_EPOLL_UPDATE, "U")
    };

    int N = Size(namemap);

    for (int i = 0; i < N; ++i)
    {
        if (events & namemap[i].first)
        {
            os << "[";
            if (et_events & namemap[i].first)
                os << "^";
            os << namemap[i].second << "]";
        }
    }

    return os;
}

template<>
int TestCipherMode::WaitOnEpoll<TestResultNonBlocking>(const TestResultNonBlocking& expect)
{
    const int default_len = 3;
    SRT_EPOLL_EVENT ready[default_len];
    const int epoll_res = srt_epoll_uwait(m_pollid, ready, default_len, 500);
    std::cerr << "Epoll wait result: " << epoll_res;
    if (epoll_res > 0)
    {
        std::cerr << " FOUND: @" << ready[0].fd << " in ";
        PrintEpollEvent(std::cerr, ready[0].events, 0);
    }
    else
    {
        std::cerr << " NOTHING READY";
    }
    std::cerr << std::endl;

    // Expect: -2 means that 
    if (expect.epoll_wait_ret != IGNORE_EPOLL)
    {
        EXPECT_EQ(epoll_res, expect.epoll_wait_ret);
    }

    if (epoll_res == SRT_ERROR)
    {
        std::cerr << "Epoll returned error: " << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
        return 0;
    }

    // We have exactly one socket here and we expect to return
    // only this one, or nothing.
    if (epoll_res != 0)
    {
        EXPECT_EQ(epoll_res, 1);
        EXPECT_EQ(ready[0].fd, m_listener_socket);
    }

    return epoll_res == 0 ? 0 : int(ready[0].events);
}


template<>
const TestCase<TestResultBlocking>& TestCipherMode::GetTestMatrix<TestResultBlocking>(TEST_CASE test_case) const
{
    return g_test_matrix_blocking[test_case];
}

template<>
const TestCase<TestResultNonBlocking>& TestCipherMode::GetTestMatrix<TestResultNonBlocking>(TEST_CASE test_case) const
{
    return g_test_matrix_non_blocking[test_case];
}



const char* TestCipherMode::m_km_state[] = {
    "SRT_KM_S_UNSECURED (0)",      //No encryption
    "SRT_KM_S_SECURING  (1)",      //Stream encrypted, exchanging Keying Material
    "SRT_KM_S_SECURED   (2)",      //Stream encrypted, keying Material exchanged, decrypting ok.
    "SRT_KM_S_NOSECRET  (3)",      //Stream encrypted and no secret to decrypt Keying Material
    "SRT_KM_S_BADSECRET (4)"       //Stream encrypted and wrong secret, cannot decrypt Keying Material        
};


static const char* const socket_state_array[] = {
    "IGNORE_SRTS",
    "SRTS_INVALID",
    "SRTS_INIT",
    "SRTS_OPENED",
    "SRTS_LISTENING",
    "SRTS_CONNECTING",
    "SRTS_CONNECTED",
    "SRTS_BROKEN",
    "SRTS_CLOSING",
    "SRTS_CLOSED",
    "SRTS_NONEXIST"
};

// A trick that allows the array to be indexed by -1
const char* const* TestCipherMode::m_socket_state = socket_state_array + 1;

/**
 * @fn TestEnforcedEncryption.SetGetDefault
 * @brief The default value for the enforced encryption should be ON
 */
//TEST_F(TestCipherMode, SetGetDefault)
//{
//    EXPECT_EQ(GetEnforcedEncryption(PEER_CALLER), true);
//    EXPECT_EQ(GetEnforcedEncryption(PEER_LISTENER), true);
//
//    EXPECT_EQ(SetEnforcedEncryption(PEER_CALLER, false), SRT_SUCCESS);
//    EXPECT_EQ(SetEnforcedEncryption(PEER_LISTENER, false), SRT_SUCCESS);
//
//    EXPECT_EQ(GetEnforcedEncryption(PEER_CALLER), false);
//    EXPECT_EQ(GetEnforcedEncryption(PEER_LISTENER), false);
//}


#define CREATE_TEST_CASE_BLOCKING(CASE_NUMBER, DESC) TEST_F(TestCipherMode, CASE_NUMBER##_Blocking_##DESC)\
{\
    TestConnect<TestResultBlocking>(TEST_##CASE_NUMBER);\
}

#define CREATE_TEST_CASE_NONBLOCKING(CASE_NUMBER, DESC) TEST_F(TestCipherMode, CASE_NUMBER##_NonBlocking_##DESC)\
{\
    TestConnect<TestResultNonBlocking>(TEST_##CASE_NUMBER);\
}


#define CREATE_TEST_CASES(CASE_NUMBER, DESC) \
    CREATE_TEST_CASE_NONBLOCKING(CASE_NUMBER, DESC) \
    CREATE_TEST_CASE_BLOCKING(CASE_NUMBER, DESC)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_A_1, Cipher_GCM_GCM_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_A_2, Cipher_GCM_GCM_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_A_3, Cipher_GCM_GCM_Pwd_Set_None)
CREATE_TEST_CASES(CASE_A_4, Cipher_GCM_GCM_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_A_5, Cipher_GCM_GCM_Pwd_None_None)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_B_1, Cipher_GCM_CTR_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_B_2, Cipher_GCM_CTR_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_B_3, Cipher_GCM_CTR_Pwd_Set_None)
CREATE_TEST_CASES(CASE_B_4, Cipher_GCM_CTR_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_B_5, Cipher_GCM_CTR_Pwd_None_None)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_C_1, Cipher_CTR_GCM_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_C_2, Cipher_CTR_GCM_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_C_3, Cipher_CTR_GCM_Pwd_Set_None)
CREATE_TEST_CASES(CASE_C_4, Cipher_CTR_GCM_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_C_5, Cipher_CTR_GCM_Pwd_None_None)




