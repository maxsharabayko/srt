#include <list>
#include <thread>
#include "srt_messaging.h"
#include "srt_receiver.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"

using namespace std;

static unique_ptr<SrtReceiver> s_rcv_srt_model;


int srt_msgn_connect(const char *uri, size_t message_size)
{
    UriParser ut(uri);

    if (ut.port().empty())
    {
        cerr << "ERROR! Check the URI provided: " << uri << endl;
        return -1;
    }

    ut["transtype"]  = string("file");
    ut["messageapi"] = string("true");
    ut["blocking"]   = string("true");
    ut["mode"]       = string("caller");

    // If we have this parameter provided, probably someone knows better
    if (!ut["sndbuf"].exists())
    {
        ut["sndbuf"] = to_string(5 * (message_size * 1472 / 1456 + 1472));
    }

    s_rcv_srt_model = unique_ptr<SrtReceiver>(new SrtReceiver(ut.host(), ut.portno(), ut.parameters()));

    const int res = s_rcv_srt_model->Connect();
    if (res == SRT_INVALID_SOCK)
    {
        cerr << "ERROR! While setting up a caller\n";
        return res;
    }

    return 0;
}


int srt_msgn_listen(const char *uri, size_t message_size)
{
    UriParser ut(uri);

    if (ut.port().empty())
    {
        cerr << "ERROR! Check the URI provided: " << uri << endl;
        return -1;
    }

    if (!ut["transtype"].exists())
        ut["transtype"]  = string("file");

    ut["messageapi"] = string("true");
    ut["blocking"]   = string("true");
    ut["mode"]       = string("listener");

    int maxconn = 5;
    if (ut["maxconn"].exists())
    {
        maxconn = std::stoi(ut.queryValue("maxconn"));
    }
    
    // If we have this parameter provided, probably someone knows better
    if (!ut["rcvbuf"].exists() && ut.queryValue("transtype") != "live")
    {
        ut["rcvbuf"] = to_string(5 * (message_size * 1472 / 1456 + 1472));
    }
    s_rcv_srt_model = std::unique_ptr<SrtReceiver>(new SrtReceiver(ut.host(), ut.portno(), ut.parameters()));

    if (!s_rcv_srt_model)
    {
        cerr << "ERROR! While creating a listener\n";
        return -1;
    }

    if (s_rcv_srt_model->Listen(maxconn) != 0)
    {
        cerr << "ERROR! While setting up a listener: " <<srt_getlasterror_str() << endl;
        return -1;
    }

    return 0;
}


int srt_msgn_send(const char *buffer, size_t buffer_len)
{
    if (!s_rcv_srt_model)
        return -1;

    return s_rcv_srt_model->Send(buffer, buffer_len);
}


int srt_msgn_send_on_conn(const char *buffer, size_t buffer_len, int connection_id)
{
    if (!s_rcv_srt_model)
        return -1;

    return s_rcv_srt_model->Send(buffer, buffer_len, connection_id);
}


int srt_msgn_wait_undelievered(int wait_ms)
{
    if (!s_rcv_srt_model)
        return SRT_ERROR;

    const SRTSOCKET sock = s_rcv_srt_model->GetBindSocket();
    const SRT_SOCKSTATUS status = srt_getsockstate(sock);
    if (status != SRTS_CONNECTED && status != SRTS_CLOSING)
        return 0;

    size_t blocks = 0;
    size_t bytes  = 0;
    int ms_passed = 0;
    do
    {
        if (SRT_ERROR == srt_getsndbuffer(sock, &blocks, &bytes))
            return SRT_ERROR;

        if (wait_ms == 0)
            break;

        if (wait_ms != -1 && ms_passed >= wait_ms)
            break;

        if (blocks)
            this_thread::sleep_for(chrono::milliseconds(1));
        ++ms_passed;
    } while (blocks != 0);

    return bytes;
}


int srt_msgn_recv(char *buffer, size_t buffer_len, int *connection_id)
{
    if (!s_rcv_srt_model)
        return -1;

    return s_rcv_srt_model->Receive(buffer, buffer_len, connection_id);
}


const char* srt_msgn_getlasterror_str(void)
{
    return srt_getlasterror_str();
}


int srt_msgn_getlasterror(void)
{
    return srt_getlasterror(NULL);
}


int srt_msgn_destroy()
{
    if (!s_rcv_srt_model)
        return 0;

    s_rcv_srt_model.reset();
    srt_cleanup();
    return 0;
}


