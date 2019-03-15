#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <thread>
#include <signal.h>
#include "srt_receiver.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "srt_node.hpp"


using namespace std;


const size_t s_message_size = 8 * 1024 * 1024;

volatile std::atomic_bool force_break = false;
volatile bool int_state = false;
volatile bool timer_state = false;
static unique_ptr<SrtReceiver> s_rcv_srt_model;
static unique_ptr<SrtReceiver> s_snd_srt_model;

void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
    force_break = true;

    if (s_rcv_srt_model)
    {
        const int undelivered = s_rcv_srt_model->WaitUndelivered(5000);
        if (undelivered)
        {
            cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
            if (undelivered == -1)
                cerr << srt_getlasterror_str() << "\n";
        }

        s_rcv_srt_model.reset();
    }

    if (s_snd_srt_model)
    {
        const int undelivered = s_snd_srt_model->WaitUndelivered(5000);
        if (undelivered)
        {
            cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
            if (undelivered == -1)
                cerr << srt_getlasterror_str() << "\n";
        }

        s_snd_srt_model.reset();
    }
    

}



/*unique_ptr<SrtNode>&&*/ SrtNode* create_node(const char *uri, bool is_caller)
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

    return new SrtNode(urlp);
}



int start_forwarding(const char *src_uri, const char *dst_uri)
{
    srt_startup();

    // Create dst connection
    unique_ptr<SrtNode> dst = unique_ptr<SrtNode>(create_node(dst_uri, true));
    if (!dst)
    {
        cerr << "ERROR! Failed to create destination node.\n";
        return 1;
    }

    unique_ptr<SrtNode> src = unique_ptr<SrtNode>(create_node(src_uri, false));
    if (!src)
    {
        cerr << "ERROR! Failed to create source node.\n";
        return 1;
    }


    // Establish target connection first
    const int sock_dst = dst->Connect();
    if (sock_dst == SRT_INVALID_SOCK)
    {
        cerr << "ERROR! While setting up a caller\n";
        return 1;
    }


    if (0 != src->Listen(1))
    {
        cerr << "ERROR! While setting up a listener: " << srt_getlasterror_str() << endl;
        return 1;
    }

    auto future_src_socket = src->AcceptConnection(force_break);
    //future_src_socket.wait();
    const SRTSOCKET sock_src = future_src_socket.get();
    if (sock_src == SRT_ERROR)
    {
        cerr << "Wait for source connection canceled\n";
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
                if (recv_res == 0 && force_break)
                    break;

                cerr << "ERROR: SRC->DST Receiving message. Result: " << recv_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (recv_res > message_rcvd.size())
            {
                cerr << "ERROR: SRC->DST Received message size " << recv_res;
                cerr << " exeeds the buffer size " << message_rcvd.size();
                cerr << " on connection: " << connection_id << "\n";
                break;
            }

            if (recv_res < 50)
            {
                cout << "SRC->DST: RECEIVED MESSAGE on conn ID " << connection_id << ":\n";
                cout << string(message_rcvd.data(), recv_res).c_str() << endl;
            }
            else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
            {
                cout << "SRC->DST: RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character):";
                cout << message_rcvd[0] << endl;
            }


            cerr << "SRC->DST Forwarding message to: " << dst->GetBindSocket() << endl;
            const int send_res = dst->Send(message_rcvd.data(), recv_res);
            if (send_res <= 0)
            {
                cerr << "ERROR: SRC->DST Forwarding message. Result: " << send_res;
                cerr << " on conn ID " << dst->GetBindSocket() << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (force_break)
            {
                cerr << "\n SRC->DST (interrupted on request)\n";
                break;
            }
        }

        cerr << "SRC->DST settint \n (interrupted on request)\n";
        src->Close();
        dst->Close();
        force_break = true;
    });


    auto th_dst_to_src = std::thread([&src, &dst, &sock_src]
    {
        vector<char> message_rcvd(s_message_size);

        while (!force_break)
        {
            int connection_id = 0;
            const int recv_res = dst->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);

            if (recv_res < 0)
            {
                if (recv_res == 0 && force_break)
                    break;

                cerr << "ERROR: DST->SRC Receiving message. Result: " << recv_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (recv_res > message_rcvd.size())
            {
                cerr << "ERROR: DST->SRC Received message size " << recv_res;
                cerr << " exeeds the buffer size " << message_rcvd.size();
                cerr << " on connection: " << connection_id << "\n";
                break;
            }

            if (recv_res < 50)
            {
                cout << "DST->SRC: RECEIVED MESSAGE on conn ID " << connection_id << ":\n";
                cout << string(message_rcvd.data(), recv_res).c_str() << endl;
            }
            else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
            {
                cout << "DST->SRC: RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character):";
                cout << message_rcvd[0] << endl;
            }

            cerr << "SRC->DST Forwarding message to: " << dst->GetBindSocket() << endl;

            const int send_res = src->Send(message_rcvd.data(), recv_res, sock_src);
            if (send_res <= 0)
            {
                cerr << "ERROR: DST->SRC Forwarding message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (force_break)
            {
                cerr << "\n DST->SRC (interrupted on request)\n";
                break;
            }
        }

        src->Close();
        dst->Close();
        cerr << "\n DST->SRC (interrupted on request)\n";
        force_break = true;
    });

    th_src_to_dst.join();
    th_dst_to_src.join();

    if (src)
    {
        const int undelivered = src->WaitUndelivered(3000);
        if (undelivered)
        {
            cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
            if (undelivered == -1)
                cerr << srt_getlasterror_str() << "\n";
        }

        src.reset();
    }

    if (dst)
    {
        const int undelivered = dst->WaitUndelivered(3000);
        if (undelivered)
        {
            cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
            if (undelivered == -1)
                cerr << srt_getlasterror_str() << "\n";
        }

        dst.reset();
    }


    return 0;


}





int connect_to(const char *uri, size_t message_size)
{
    UriParser ut(uri);

    if (ut.port().empty())
    {
        cerr << "ERROR! Check the URI provided: " << uri << endl;
        return -1;
    }

    ut["transtype"] = string("file");
    ut["messageapi"] = string("true");
    ut["mode"] = string("caller");

    // If we have this parameter provided, probably someone knows better
    if (!ut["sndbuf"].exists())
    {
        ut["sndbuf"] = to_string(3 * (message_size * 1472 / 1456 + 1472));
    }

    s_snd_srt_model = unique_ptr<SrtReceiver>(new SrtReceiver(ut.host(), ut.portno(), ut.parameters()));

    const int res = s_snd_srt_model->Connect();
    if (res == SRT_INVALID_SOCK)
    {
        cerr << "ERROR! While setting up a caller\n";
        return res;
    }

    return 0;
}


int listen_to(const char *uri, size_t message_size)
{
    UriParser ut(uri);

    if (ut.port().empty())
    {
        cerr << "ERROR! Check the URI provided: " << uri << endl;
        return -1;
    }

    if (!ut["transtype"].exists())
        ut["transtype"] = string("file");

    ut["messageapi"] = string("true");
    ut["mode"] = string("listener");

    int maxconn = 5;
    if (ut["maxconn"].exists())
    {
        maxconn = std::stoi(ut.queryValue("maxconn"));
    }

    // If we have this parameter provided, probably someone knows better
    if (!ut["rcvbuf"].exists() && ut.queryValue("transtype") != "live")
    {
        ut["rcvbuf"] = to_string(3 * (message_size * 1472 / 1456 + 1472));
    }
    s_rcv_srt_model = std::unique_ptr<SrtReceiver>(new SrtReceiver(ut.host(), ut.portno(), ut.parameters()));

    if (!s_rcv_srt_model)
    {
        cerr << "ERROR! While creating a listener\n";
        return -1;
    }

    if (s_rcv_srt_model->Listen(maxconn) != 0)
    {
        cerr << "ERROR! While setting up a listener: " << srt_getlasterror_str() << endl;
        return -1;
    }

    return 0;
}







int create_connection(const char *uri_src, const char *uri_dst)
{
    // First we create the destination connection.
    const int conn_res = connect_to(uri_dst, s_message_size);
    if (conn_res != 0)
        return conn_res;

    const int lstn_res = listen_to(uri_src, s_message_size);
    if (lstn_res != 0)
        return lstn_res;

    s_rcv_srt_model->WaitUntilSocketAccepted();
    {
        set<SRTSOCKET> src_sockets = s_rcv_srt_model->GetAcceptedSockets();
        if (src_sockets.size() != 1)
        {
            cerr << "ERROR: src_sockets.size(): " << src_sockets.size() << endl;
            return 1;
        }
        cerr << "RCV received connection\n";
    }


    auto th_src_to_dst = std::thread([]
    {
        vector<char> message_rcvd(s_message_size);

        while (true)
        {
            int connection_id = 0;
            const int recv_res = s_rcv_srt_model->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);
            if (recv_res <= 0)
            {
                cerr << "ERROR: RCV Receiving message. Result: " << recv_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (recv_res > message_rcvd.size())
            {
                cerr << "ERROR: RCV Received message size " << recv_res;
                cerr << " exeeds the buffer size " << message_rcvd.size();
                cerr << " on connection: " << connection_id << "\n";
                break;
            }

            const int send_res = s_snd_srt_model->Send(message_rcvd.data(), recv_res);
            if (send_res <= 0)
            {
                cerr << "ERROR: RCV Forwarding message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (force_break)
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }
        }

        cerr << "Receiver set force break\n";
        force_break = true;
    });


    auto th_dst_to_src = std::thread([]
    {
        vector<char> message_rcvd(s_message_size);

        while (true)
        {
            set<SRTSOCKET> src_sockets = s_rcv_srt_model->GetAcceptedSockets();
            if (src_sockets.size() != 1)
            {
                cerr << "ERROR: SND impossible due to " << src_sockets.size() << " source sockets\n";
                break;
            }
            const SRTSOCKET src_socket = *src_sockets.begin();

            int connection_id = 0;
            const int recv_res = s_snd_srt_model->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);
            if (recv_res <= 0)
            {
                cerr << "ERROR: SND Receiving message. Result: " << recv_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (recv_res > message_rcvd.size())
            {
                cerr << "ERROR: RCV Received message size " << recv_res;
                cerr << " exeeds the buffer size " << message_rcvd.size();
                cerr << " on connection: " << connection_id << "\n";
                break;
            }

            const int send_res = s_rcv_srt_model->Send(message_rcvd.data(), recv_res, src_socket);
            if (send_res <= 0)
            {
                cerr << "ERROR: SND Forwarding message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                break;
            }

            if (force_break)
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }
        }

        cerr << "SENDER set force break\n";
        force_break = true;
    });


    th_src_to_dst.join();
    th_dst_to_src.join();
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
    if (argc == 1)
    {
        print_help();
        return 1;
    }

    // The message part can contain 'help' substring,
    // but it is expected to be in argv[2].
    // So just search for a substring.
    if (nullptr != strstr(argv[1], "help"))
    {
        print_help();
        return 0;
    }

    if (argc > 3)
    {
        print_help();
        return 0;
    }

    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);

    return start_forwarding(argv[1], argv[2]);
    //create_connection(argv[1], argv[2]);

    return 0;
}

