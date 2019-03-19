#include <thread>
#include <list>
#include <queue>
#include <atomic>
#include <mutex>
#include "srt.h"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "utilities.h"


class SrtReceiver
{

public:

    SrtReceiver();

    ~SrtReceiver();

    int Listen(const std::string &host, int port,
        const std::map<string, string> &options, int max_conn);

    int Connect(const std::string &host, int port,
        const std::map<string, string> &options);

    int Close();


    // Receive data
    // return     -2 unexpected error
    //            -1 SRT error
    //
    int Receive(char *buffer, size_t buffer_len, int *srt_socket_id);


    int Send(const char *buffer, size_t buffer_len, int srt_socket_id);


    int Send(const char *buffer, size_t buffer_len);


public:

    SRTSOCKET GetBindSocket() { return m_bindsock; }

    int WaitUndelivered(int wait_ms);

private:

    int EstablishConnection(const std::string &host, int port,
        const std::map<string, string> &options, bool caller, int max_conn);

    void AcceptingThread(const std::map<string, string> options);

    SRTSOCKET AcceptNewClient(const std::map<string, string> &options);

    int ConfigurePre(SRTSOCKET sock, const std::map<string, string> &options);
    int ConfigureAcceptedSocket(SRTSOCKET sock, const std::map<string, string> &options);



private:    // Reading manipulation helper functions

    void UpdateReadFIFO(const int rnum, const int wnum);

private:

    std::list<SRTSOCKET>      m_read_fifo;

    std::vector<SRTSOCKET>    m_epoll_read_fds;
    std::vector<SRTSOCKET>    m_epoll_write_fds;
    SRTSOCKET                 m_bindsock = SRT_INVALID_SOCK;
    int                       m_epoll_accept = -1;
    int                       m_epoll_receive = -1;

private:

    std::atomic<bool> m_stop_accept = { false };
    std::mutex        m_recv_mutex;

    std::thread m_accepting_thread;

};

