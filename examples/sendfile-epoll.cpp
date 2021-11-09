#ifndef _WIN32
#include <cstdlib>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <array>
#include <fstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <map>
#include <sstream>

#include <srt.h>

using namespace std;

#ifndef _WIN32
void* sendfile(void*);
#else
DWORD WINAPI sendfile(LPVOID);
#endif

inline bool SplitKeyValuePairs(const std::string & str, char delimiter, map<char, string> & pairs)
{
    stringstream ss(str);
    string item;
    while (getline(ss, item, delimiter))
    {
        string key;
        string value;
        stringstream itemss(item);
        getline(itemss, key, '=');
        if (key.size() != 1)
        {
            cerr << "Invalid key: " << key << endl;
            return false;
        }
        getline(itemss, value);
        pairs[key[0]] = value;
    }
    return true;
}

/// @returns true if a connection can be accepted; false otherwise.
bool ParseStreamID(const char* streamid)
{
    if (streamid == nullptr)
    {
        cerr << "Stream ID is NULL" << endl;
        return false;
    }

    const int streamid_len = strlen(streamid);

    static const char* markuphdr = "#!::";
    const int hdr_size = 4;
    if (streamid_len <= hdr_size || *reinterpret_cast<const uint32_t*>(streamid) != *reinterpret_cast<const uint32_t*>(markuphdr))
    {
        cerr << "Unexpected StreamID format\n";
        return false;
    }

    map<char, string> pairs;
    if (!SplitKeyValuePairs(streamid + hdr_size, ',', pairs))
    {
        return false;
    }

    const array<char, 3> required_keys = {'r', 't', 'm'};
    for (const auto& key : required_keys)
    {
        if (pairs.count(key) != 0)
            continue;

        cerr << "AccessControl: Missing key " << key << ". Rejecting.\n";
        return false;
    }

    for (const auto& [key, value]: pairs)
    {
        if (key == 't')
        {
            if (value != "file")
            {
                cerr << "AccessControl: Expected type 'file', got " << value << ". Rejected.\n";
                return false;
            }
        }
        else if (key == 'm')
        {
            if (value != "request")
            {
                cerr << "AccessControl: Expected method 'request', got " << value << ". Rejected.\n";
                return false;
            }
        }
        else if (key == 'r')
        {
            // Check file exists
            ifstream ifs(value);
            if (!ifs.good())
            {
                cerr << "AccessControl: File " << value << " does not exist. Rejected.\n";
                return false;
            }   
        }
        else
        {
            cerr << "AccessControl: Unexpected token: " << key << "=" << value << ". Skipping.\n";
        }
    }
    return true;
}

// The callback function is called once a listener socket receives
// a new connection request.
int SrtConnectionRequestCallback(void*, SRTSOCKET newsocj, int hsversion,
    const struct sockaddr* peeraddr, const char* streamid)
{
    if (!ParseStreamID(streamid))
        return -1;
    return 0;
}

int main(int argc, char* argv[])
{
    //usage: sendfile [server_port]
    if ((2 < argc) || ((2 == argc) && (0 == atoi(argv[1]))))
    {
        cout << "usage: sendfile [server_port]" << endl;
        return 0;
    }

    // use this function to initialize the UDT library
    srt_startup();

    srt_setloglevel(srt_logging::LogLevel::debug);

    addrinfo hints;
    addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    const string service = (2 == argc)
        ? argv[1]
        : "9000";

    if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
    {
        cout << "illegal port number or port is busy.\n" << endl;
        return 0;
    }

    SRTSOCKET serv = srt_create_socket();

    // SRT requires that third argument is always SOCK_DGRAM. The Stream API is set by an option,
    // although there's also lots of other options to be set, for which there's a convenience option,
    // SRTO_TRANSTYPE.
    SRT_TRANSTYPE tt = SRTT_FILE;
    srt_setsockopt(serv, 0, SRTO_TRANSTYPE, &tt, sizeof tt);

    if (SRT_ERROR == srt_bind(serv, res->ai_addr, res->ai_addrlen))
    {
        cout << "bind: " << srt_getlasterror_str() << endl;
        return 0;
    }

    freeaddrinfo(res);

    cout << "server is ready at port: " << service << endl;

    srt_listen_callback(serv, &SrtConnectionRequestCallback, NULL);
    srt_listen(serv, 10);

    sockaddr_storage clientaddr;
    int addrlen = sizeof(clientaddr);

    SRTSOCKET accepted_sock;

    while (true)
    {
        if (SRT_INVALID_SOCK == (accepted_sock = srt_accept(serv, (sockaddr*)&clientaddr, &addrlen)))
        {
            cout << "accept: " << srt_getlasterror_str() << endl;
            return 0;
        }

        char clienthost[NI_MAXHOST];
        char clientservice[NI_MAXSERV];
        getnameinfo((sockaddr*)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST | NI_NUMERICSERV);
        cout << "new connection: " << clienthost << ":" << clientservice << endl;

#ifndef _WIN32
        pthread_t filethread;
        pthread_create(&filethread, NULL, sendfile, new SRTSOCKET(accepted_sock));
        pthread_detach(filethread);
#else
        CreateThread(NULL, 0, sendfile, new SRTSOCKET(accepted_sock), 0, NULL);
#endif
    }

    srt_close(serv);

    // use this function to release the UDT library
    srt_cleanup();

    return 0;
}

#ifndef _WIN32
void* sendfile(void* usocket)
#else
DWORD WINAPI sendfile(LPVOID usocket)
#endif
{
    SRTSOCKET fhandle = *(SRTSOCKET*)usocket;
    delete (SRTSOCKET*)usocket;

    // aquiring file name information from client
    //char file[1024];

    char streamid[512];
    int streamid_len = sizeof streamid;

    srt_getsockflag(fhandle, SRTO_STREAMID, (void*)streamid, &streamid_len);

    // streamid: #!::t=file,m=request,r=
    const char* file = streamid + 23;
    // open the file (only to check the size)
    fstream ifs(file, ios::in | ios::binary);

    ifs.seekg(0, ios::end);
    if (!ifs)
    {
        cout << "server: file " << file << " not found\n";
        return 0;
    }

    int64_t size = ifs.tellg();
    //ifs.seekg(0, ios::beg);
    ifs.close();

    cout << "server: sending file " << file << " (" << size << " bytes)\n";

    // send file size information
    //if (SRT_ERROR == srt_send(fhandle, (char*)&size, sizeof(int64_t)))
    //{
    //    cout << "send: " << srt_getlasterror_str() << endl;
    //    return 0;
    //}

    SRT_TRACEBSTATS trace;
    srt_bstats(fhandle, &trace, true);

    // send the file
    int64_t offset = 0;
    if (SRT_ERROR == srt_sendfile(fhandle, file, &offset, size, SRT_DEFAULT_SENDFILE_BLOCK))
    {
        cout << "sendfile: " << srt_getlasterror_str() << endl;
        return 0;
    }

    srt_bstats(fhandle, &trace, true);
    cout << "speed = " << trace.mbpsSendRate << "Mbits/sec" << endl;
    int losspercent = 100 * trace.pktSndLossTotal / trace.pktSent;
    cout << "loss = " << trace.pktSndLossTotal << "pkt (" << losspercent << "%)\n";

    srt_close(fhandle);

#ifndef _WIN32
    return NULL;
#else
    return 0;
#endif
}
