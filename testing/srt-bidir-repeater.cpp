#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <thread>
#include <signal.h>
#include "srt_receiver.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"

using namespace std;


const size_t s_message_size = 8 * 1024 * 1024;

volatile bool int_state = false;
volatile bool timer_state = false;
static unique_ptr<SrtReceiver> s_rcv_srt_model;
static unique_ptr<SrtReceiver> s_snd_srt_model;

void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;

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
    ut["blocking"] = string("true");
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
    ut["blocking"] = string("true");
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

    //const int wait_loops = 5000;
    //for (int i = 0; i < wait_loops; ++i)
    //{
    //    set<SRTSOCKET> src_sockets = s_rcv_srt_model->GetAcceptedSockets();
    //    cerr << "src_sockets.size(): " << src_sockets.size() << endl;
    //    if (src_sockets.size() == 1)
    //    {
    //        cerr << "RCV received connection\n";
    //        break;
    //    }
    //    
    //    if (i == wait_loops - 1)
    //    {
    //        cerr << "ERROR: No connection was received after 5 sec. Closing.\n";
    //        return 1;
    //    }

    //    this_thread::sleep_for(chrono::milliseconds(1));
    //}


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

                return;
            }

            if (recv_res > message_rcvd.size())
            {
                cerr << "ERROR: RCV Received message size " << recv_res;
                cerr << " exeeds the buffer size " << message_rcvd.size();
                cerr << " on connection: " << connection_id << "\n";
                return;
            }

            const int send_res = s_snd_srt_model->Send(message_rcvd.data(), recv_res);
            if (send_res <= 0)
            {
                cerr << "ERROR: RCV Forwarding message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                return;
            }

            if (int_state)
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }
        }
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

                return;
            }

            if (recv_res > message_rcvd.size())
            {
                cerr << "ERROR: RCV Received message size " << recv_res;
                cerr << " exeeds the buffer size " << message_rcvd.size();
                cerr << " on connection: " << connection_id << "\n";
                return;
            }

            const int send_res = s_rcv_srt_model->Send(message_rcvd.data(), recv_res, src_socket);
            if (send_res <= 0)
            {
                cerr << "ERROR: SND Forwarding message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_getlasterror_str() << endl;

                return;
            }

            if (int_state)
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }
        }
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

    create_connection(argv[1], argv[2]);

    return 0;
}

