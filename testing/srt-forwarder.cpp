#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <thread>
#include <signal.h>

#include "apputil.hpp"
#include "srt_receiver.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "srt_node.hpp"
#include "logging.h"
#include "logsupport.hpp"
#include "verbose.hpp"


using namespace std;



const srt_logging::LogFA SRT_LOGFA_FORWARDER = 10;
srt_logging::Logger g_applog(SRT_LOGFA_FORWARDER, srt_logger_config, "srt-fwd");


const size_t s_message_size = 8 * 1024 * 1024;

volatile std::atomic_bool force_break = false;


void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    force_break = true;
}



unique_ptr<SrtNode> create_node(const char *uri, bool is_caller)
{
    UriParser urlp(uri);
    urlp["transtype"]  = string("file");
    urlp["messageapi"] = string("true");
    urlp["mode"]       = is_caller ? string("caller") : string("listener");

    // If we have this parameter provided, probably someone knows better
    if (!urlp["sndbuf"].exists())
        urlp["sndbuf"] = to_string(3 * (s_message_size * 1472 / 1456 + 1472));
    if (!urlp["rcvbuf"].exists())
        urlp["rcvbuf"] = to_string(3 * (s_message_size * 1472 / 1456 + 1472));

    return unique_ptr<SrtNode>(new SrtNode(urlp));
}



int start_forwarding(const char *src_uri, const char *dst_uri)
{
    srt_startup();

    // Create dst connection
    unique_ptr<SrtNode> dst = create_node(dst_uri, true);
    if (!dst)
    {
        g_applog.Error() << "ERROR! Failed to create destination node.\n";
        return 1;
    }

    unique_ptr<SrtNode> src = create_node(src_uri, false);
    if (!src)
    {
        g_applog.Error() << "ERROR! Failed to create source node.\n";
        return 1;
    }


    // Establish target connection first
    const int sock_dst = dst->Connect();
    if (sock_dst == SRT_INVALID_SOCK)
    {
        g_applog.Error() << "ERROR! While setting up a caller\n";
        return 1;
    }


    if (0 != src->Listen(1))
    {
        g_applog.Error() << "ERROR! While setting up a listener: " << srt_getlasterror_str();
        return 1;
    }

    auto future_src_socket = src->AcceptConnection(force_break);
    //future_src_socket.wait();
    const SRTSOCKET sock_src = future_src_socket.get();
    if (sock_src == SRT_ERROR)
    {
        g_applog.Error() << "Wait for source connection canceled\n";
        return 0;
    }


    auto th_src_to_dst = std::thread([&src, &dst]
    {
        vector<char> message_rcvd(s_message_size);

        while (!force_break)
        {
            int connection_id = 0;
            const int recv_res = src->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);
            if (recv_res <= 0)
            {
                if (recv_res == 0 && connection_id == 0)
                    break;

                g_applog.Error() << "ERROR: SRC->DST Receiving message. Result: " << recv_res
                                 << " on conn ID " << connection_id << "\n";
                g_applog.Error() << srt_getlasterror_str();

                break;
            }

            if (recv_res > message_rcvd.size())
            {
                g_applog.Error() << "ERROR: SRC->DST Received message size " << recv_res
                                 << " exeeds the buffer size " << message_rcvd.size();
                g_applog.Error() << " on connection: " << connection_id << "\n";
                break;
            }

            if (recv_res < 50)
            {
                g_applog.Debug() << "SRC->DST: RECEIVED MESSAGE on conn ID " << connection_id << ":\n";
                g_applog.Debug() << string(message_rcvd.data(), recv_res).c_str();
            }
            else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
            {
                g_applog.Debug() << "SRC->DST: RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character):";
                g_applog.Debug() << message_rcvd[0];
            }


            g_applog.Debug() << "SRC->DST Forwarding message to: " << dst->GetBindSocket();
            const int send_res = dst->Send(message_rcvd.data(), recv_res);
            if (send_res <= 0)
            {
                g_applog.Error() << "ERROR: SRC->DST Forwarding message. Result: " << send_res
                                 << " on conn ID " << dst->GetBindSocket() << "\n";
                g_applog.Error() << srt_getlasterror_str();

                break;
            }

            if (force_break)
            {
                g_applog.Debug() << "\n SRC->DST (interrupted on request)\n";
                break;
            }
        }

        src->Close();
        dst->Close();
        if (!force_break)
        {
            g_applog.Debug() << "SRC->DST set int \n (interrupted on request)\n";
            force_break = true;
        }
    });


    auto th_dst_to_src = std::thread([&src, &dst, &sock_src]
    {
        vector<char> message_rcvd(s_message_size);

        while (!force_break)
        {
            int connection_id = 0;
            const int recv_res = dst->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);

            if (recv_res <= 0)
            {
                if (recv_res == 0 && connection_id == 0)
                    break;

                g_applog.Error() << "ERROR: DST->SRC Receiving message. Result: " << recv_res
                                 << " on conn ID " << connection_id << "\n";
                g_applog.Error() << srt_getlasterror_str();

                break;
            }

            if (recv_res > message_rcvd.size())
            {
                g_applog.Error() << "ERROR: DST->SRC Received message size " << recv_res
                                 << " exeeds the buffer size " << message_rcvd.size();
                g_applog.Error() << " on connection: " << connection_id << "\n";
                break;
            }

            if (recv_res < 50)
            {
                g_applog.Debug() << "DST->SRC: RECEIVED MESSAGE on conn ID " << connection_id << ":\n";
                g_applog.Debug() << string(message_rcvd.data(), recv_res).c_str();
            }
            else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
            {
                g_applog.Debug() << "DST->SRC: RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character):";
                g_applog.Debug() << message_rcvd[0];
            }

            g_applog.Debug() << "SRC->DST Forwarding message to: " << dst->GetBindSocket();

            const int send_res = src->Send(message_rcvd.data(), recv_res, sock_src);
            if (send_res <= 0)
            {
                g_applog.Error() << "ERROR: DST->SRC Forwarding message. Result: " << send_res
                                 << " on conn ID " << connection_id << "\n";
                g_applog.Error() << srt_getlasterror_str();

                break;
            }

            if (force_break)
            {
                g_applog.Debug() << "\n DST->SRC (interrupted on request)\n";
                break;
            }
        }

        src->Close();
        dst->Close();
        if (!force_break)
        {
            g_applog.Debug() << "\n DST->SRC (interrupted on request)\n";
            force_break = true;
        }
    });

    th_src_to_dst.join();
    th_dst_to_src.join();

    if (src)
    {
        const int undelivered = src->WaitUndelivered(3000);
        if (undelivered)
        {
            g_applog.Error() << "ERROR: src Still has undelivered bytes " << undelivered << "\n";
            if (undelivered == -1)
                g_applog.Error() << srt_getlasterror_str() << "\n";
        }

        src.reset();
    }

    if (dst)
    {
        const int undelivered = dst->WaitUndelivered(3000);
        if (undelivered)
        {
            g_applog.Error() << "ERROR:  dst Still has undelivered bytes " << undelivered << "\n";
            if (undelivered == -1)
                g_applog.Error() << srt_getlasterror_str() << "\n";
        }

        dst.reset();
    }


    return 0;


}






void print_help()
{
    cout << "The CLI syntax is\n"
         << "    Run autotest: no arguments required\n"
         << "  Two peers test:\n"
         << "    Send:    srt-test-messaging \"srt://ip:port\" \"message\"\n"
         << "    Receive: srt-test-messaging \"srt://ip:port\"\n";
}


int main(int argc, char** argv)
{
    // This is mainly required on Windows to initialize the network system,
    // for a case when the instance would use UDP. SRT does it on its own, independently.
    if (!SysInitializeNetwork())
        throw std::runtime_error("Can't initialize network!");

    // Symmetrically, this does a cleanup; put into a local destructor to ensure that
    // it's called regardless of how this function returns.
    struct NetworkCleanup
    {
        ~NetworkCleanup()
        {
            SysCleanupNetwork();
        }
    } cleanupobj;


    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);

    // Check options
    vector<OptionScheme> optargs = {
        { {"ll", "loglevel"}, OptionScheme::ARG_ONE },
    };
    map<string, vector<string>> params = ProcessOptions(argv, argc, optargs);


    if (params.count("-help") || params.count("-h"))
    {
        print_help();
        return 1;
    }

    if (params[""].empty())
    {
        print_help();
        return 1;
    }

    if (params[""].size() != 2)
    {
        cerr << "Extra parameter after the first one: " << Printable(params[""]) << endl;
        print_help();
        return 1;
    }

    const string loglevel = Option<OutString>(params, "error", "ll", "loglevel");
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    UDT::addlogfa(SRT_LOGFA_FORWARDER);

    if (params.count("v"))
        Verbose::on = true;

    return start_forwarding(params[""][0].c_str(), params[""][1].c_str());
}

