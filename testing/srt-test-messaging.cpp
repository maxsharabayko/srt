#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <thread>
#include <signal.h>
#include "srt_messaging.h"

using namespace std;


const size_t s_message_size = 8 * 1024 * 1024;

volatile bool int_state = false;
volatile bool timer_state = false;
void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
    const int undelivered = srt_msgn_wait_undelievered(5000);
    if (undelivered)
    {
        cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
        if (undelivered == -1)
            cerr << srt_msgn_getlasterror_str() << "\n";
    }
    srt_msgn_destroy();
}


void test_messaging_localhost()
{
    const size_t &message_size = s_message_size;
    srt_msgn_listen("srt://:4200?maxconn=4", message_size);

    vector<char> message_rcvd(message_size);
    bool rcv_error = false;

    auto rcv_thread = std::thread([&message_rcvd, &rcv_error, &message_size]
    {
        int connection_id = 0;
        const int recv_res = srt_msgn_recv(message_rcvd.data(), message_rcvd.size(), &connection_id);
        if (recv_res != (int) message_size)
        {
            cerr << "ERROR: Receiving " << message_size << ", received " << recv_res;
            cerr << " on conn ID " << connection_id << "\n";
            cerr << srt_msgn_getlasterror_str();
            rcv_error = true;
        }
    });

    // This should block untill we get connected
    srt_msgn_connect("srt://127.0.0.1:4200", message_size);

    // Now we are connected, start sending the message
    vector<char> message_sent(message_size);
    char c = 0;
    for (size_t i = 0; i < message_sent.size(); ++i)
    {
        message_sent[i] = c++;
    }

    const int sent_res = srt_msgn_send(message_sent.data(), message_sent.size());
    if (sent_res != (int) message_size)
    {
        cerr << "ERROR: Sending " << message_size << ", sent " << sent_res << "\n";
        return;
    }

    // Wait for another thread to receive the message (receiving thread)
    rcv_thread.join();

    if (rcv_error)
    {
        // There was an error when receiving. No need to check the message
        return;
    }

    bool mismatch_found = false;
    for (size_t i = 0; i < message_sent.size(); ++i)
    {
        if (message_sent[i] == message_rcvd[i])
            continue;

        mismatch_found = true;
        cerr << "ERROR: Pos " << i
            << " received " << int(message_rcvd[i]) << ", actually sent " << int(message_sent[i]) << "\n";
    }

    if (!mismatch_found)
        cerr << "Check passed\n";

    srt_msgn_destroy();
}


void receive_message(const char *uri)
{
    cout << "Listen to " << uri << "\n";

    const size_t &message_size = s_message_size;
    if (0 != srt_msgn_listen(uri, message_size))
    {
        cerr << "ERROR: Listen failed.\n";

        srt_msgn_destroy();
        return;
    }

    vector<char> message_rcvd(message_size);

    //this_thread::sleep_for(chrono::seconds(5));

    try
    {
        while (true)
        {
            int connection_id = 0;
            const int recv_res = srt_msgn_recv(message_rcvd.data(), message_rcvd.size(), &connection_id);
            if (recv_res <= 0)
            {
                cerr << "ERROR: Receiving message. Result: " << recv_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_msgn_getlasterror_str() << endl;

                srt_msgn_destroy();
                return;
            }

            if (recv_res < 50)
            {
                cout << "RECEIVED MESSAGE on conn ID " << connection_id << ":\n";
                cout << string(message_rcvd.data(), recv_res).c_str() << endl;
            }
            else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
            {
                cout << "RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character):";
                cout << message_rcvd[0] << endl;
            }

            const string out_message("Message received");
            const int send_res = srt_msgn_send_on_conn(out_message.data(), out_message.size(), connection_id);
            if (send_res <= 0)
            {
                cerr << "ERROR: Sending reply message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_msgn_getlasterror_str() << endl;

                srt_msgn_destroy();
                return;
            }
            cout << "Reply sent on conn ID " << connection_id << ":\n";

            if (int_state)
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }
        }
    }
    catch (std::exception &ex)
    {
        cerr<< "EXCEPTION: " << ex.what() << endl;
    }


    srt_msgn_destroy();
}


void send_message(const char *uri, const char* message, size_t length)
{
    cout << "Connect to " << uri << "\n";
    const size_t message_size = 8 * 1024 * 1024;
    if (-1 == srt_msgn_connect(uri, message_size))
    {
        cerr << "ERROR: Connect failed.\n";
        srt_msgn_destroy();
        return;
    }

    int sent_res = srt_msgn_send(message, length);
    if (sent_res != (int) length)
    {
        cerr << "ERROR: Sending message " << length << ". Result: " << sent_res << "\n";
        cerr << srt_msgn_getlasterror_str() << endl;
        srt_msgn_destroy();
        return;
    }

    cout << "SENT MESSAGE:\n";
    cout << message << endl;

    vector<char> message_to_send(message_size);
    char c = 0;
    for (size_t i = 0; i < message_to_send.size(); ++i)
    {
        message_to_send[i] = c++;
    }

    for (int i = 0; i < 5; ++i)
    {
        message_to_send[0] = '0' + i;
        sent_res = srt_msgn_send(message_to_send.data(), message_to_send.size());
        if (sent_res != (int)message_size)
        {
            cerr << "ERROR: Sending " << message_size << ", sent " << sent_res << "\n";
            cerr << srt_msgn_getlasterror_str() << endl;
            break;
        }
        cout << "SENT MESSAGE #" << i << "\n";
    }

    for (int i = 0; i < 6; ++i)
    {
        cout << "WAITING FOR MESSAGE no." << i << "\n";
        const int rcv_res = srt_msgn_recv(message_to_send.data(), message_to_send.size(), nullptr);
        if (rcv_res <= 0)
        {
            cerr << "ERROR: Receiving message. Result: " << rcv_res << "\n";
            cerr << srt_msgn_getlasterror_str() << endl;
            srt_msgn_destroy();
            return;
        }

        cout << "RECEIVED MESSAGE no." << i << ":\n";
        cout << string(message_to_send.data(), rcv_res).c_str() << endl;
    }

    const int undelivered = srt_msgn_wait_undelievered(5000);
    if (undelivered)
    {
        cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
    }
    srt_msgn_destroy();
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
        test_messaging_localhost();
        return 0;
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

    if (argc == 2)
        receive_message(argv[1]);
    else
        send_message(argv[1], argv[2], strnlen(argv[2], s_message_size));

    return 0;
}

