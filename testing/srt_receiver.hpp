#include <thread>
#include <list>
#include <queue>
#include <atomic>
#include "srt.h"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "utilities.h"


class SrtReceiver
    //: public SrtCommon
{

public:

    SrtReceiver(std::string host, int port, std::map<string, string> par);

    ~SrtReceiver();

    int Listen(int max_conn);

    // Receive data
    // return     -2 unexpected error
    //            -1 SRT error
    //
    int Receive(char *buffer, size_t buffer_len, int *srt_socket_id);


    int Send(const char *buffer, size_t buffer_len, int srt_socket_id);


private:

    void AcceptingThread();

    SRTSOCKET AcceptNewClient();

    int ConfigurePre(SRTSOCKET sock);
    int ConfigureAcceptedSocket(SRTSOCKET sock);


private:    // Reading manipulation helper functions

    void UpdateReadFIFO(const int rnum, const int wnum);

private:

    std::list<SRTSOCKET>      m_read_fifo;

    std::vector<SRTSOCKET>    m_epoll_read_fds;
    std::vector<SRTSOCKET>    m_epoll_write_fds;
    SRTSOCKET                 m_bindsock      = SRT_INVALID_SOCK;
    int                       m_epoll_accept  = -1;
    int                       m_epoll_receive = -1;

private:

    std::atomic<bool> m_stop_accept = { false };

    std::thread m_accepting_thread;

private:    // Configuration

    std::string m_host;
    int m_port;
    std::map<string, string> m_options; // All other options, as provided in the URI

};


