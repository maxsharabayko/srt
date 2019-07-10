#include <list>
#include <thread>
#include <atomic>
#include "srt_messaging.h"
#include "srt_receiver.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "verbose.hpp"

using namespace std;

unique_ptr<SrtReceiver> g_rcv_srt_model;


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
    ut["congestion"] = string("filev2");

    // If we have this parameter provided, probably someone knows better
    if (!ut["sndbuf"].exists())
    {
        ut["sndbuf"] = string("625000000");
    }
    if (!ut["rcvbuf"].exists())
    {
        ut["rcvbuf"] = string("625000000");
    }
    if (!ut["fc"].exists())
    {
        ut["fc"] = string("60000");
    }

    if (!g_rcv_srt_model)
        g_rcv_srt_model = unique_ptr<SrtReceiver>(new SrtReceiver());

    const int res = g_rcv_srt_model->Connect(ut.host(), ut.portno(), ut.parameters());
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
    ut["congestion"] = string("filev2");

    int maxconn = 5;
    if (ut["maxconn"].exists())
    {
        maxconn = std::stoi(ut.queryValue("maxconn"));
    }
    
    // If we have this parameter provided, probably someone knows better
    if (!ut["rcvbuf"].exists() && ut.queryValue("transtype") != "live")
    {
        ut["rcvbuf"] = string("625000000");
    }
    if (!ut["sndbuf"].exists())
    {
        ut["sndbuf"] = string("625000000");
    }
    if (!ut["fc"].exists())
    {
        ut["fc"] = string("60000");
    }

    if (!g_rcv_srt_model)
        g_rcv_srt_model = unique_ptr<SrtReceiver>(new SrtReceiver());

    if (g_rcv_srt_model->Listen(ut.host(), ut.portno(), ut.parameters(), maxconn) != 0)
    {
        cerr << "ERROR! While setting up a listener: " <<srt_getlasterror_str() << endl;
        return -1;
    }

    return 0;
}


int srt_msgn_send(const char *buffer, size_t buffer_len)
{
    if (!g_rcv_srt_model)
        return -1;

    return g_rcv_srt_model->Send(buffer, buffer_len);
}


int srt_msgn_send_on_conn(const char *buffer, size_t buffer_len, int connection_id)
{
    if (!g_rcv_srt_model)
        return -1;

    return g_rcv_srt_model->Send(buffer, buffer_len, connection_id);
}


int srt_msgn_wait_undelievered(int wait_ms)
{
    if (!g_rcv_srt_model)
        return -1;

    return g_rcv_srt_model->WaitUndelivered(wait_ms);
}


int srt_msgn_recv(char *buffer, size_t buffer_len, int *connection_id)
{
    if (!g_rcv_srt_model)
        return -1;

    return g_rcv_srt_model->Receive(buffer, buffer_len, connection_id);
}


int srt_msgn_bstats(SRT_PERF_STATS* stats, int connection_id, int clear)
{
    if (stats == nullptr)
        return -1;

    if (!g_rcv_srt_model)
        return -1;

    SRT_TRACEBSTATS stats_srt;

    SRTSOCKET sock = connection_id != SRT_INVALID_SOCK
        ? connection_id
        : g_rcv_srt_model->GetBindSocket();

    const int res = srt_bstats(sock, &stats_srt, clear);

    memcpy(stats, &stats_srt, sizeof(SRT_TRACEBSTATS));
    return res;
}


int srt_msgn_set_loglevel(int loglevel, int verbose)
{
    srt_logging::LogLevel::type lev = (srt_logging::LogLevel::type) loglevel;
    UDT::setloglevel(lev);

    if (verbose)
        Verbose::on = true;

    return 0;
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
    if (!g_rcv_srt_model)
        return -1;

    const int ret = g_rcv_srt_model->Close();
    g_rcv_srt_model.reset();
    return ret;
}


