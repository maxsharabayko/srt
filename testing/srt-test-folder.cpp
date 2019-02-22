/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

 /*****************************************************************************
 written by
    Haivision Systems Inc.
  *****************************************************************************/

#ifdef _WIN32
#include <direct.h>
#include "dirent.h"
#else
#include <dirent.h>
#include <errno.h>
#if defined(__GNUC__) && (__GNUC___ < 5)
#define ONLY_C_LOCALE 1
  // In date.h replace fields() = default;
  // with fields() : ymd{year{0}/0/0}, wd{7u} {}
#endif

#endif

#include <iostream>
#include <iterator>
#include <vector>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>

#include "apputil.hpp"
#include "uriparser.hpp"
#include "logsupport.hpp"
#include "socketoptions.hpp"
#include "verbose.hpp"
#include "testmedia.hpp"

#if ENABLE_JSON_INPUT
#include "nlohmann/json.hpp"
#include "date/date.h"
using json = nlohmann::json;
#endif

using namespace std;

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

bool Upload(UriParser& srt, UriParser& file);
bool Download(UriParser& srt, UriParser& file);

const srt_logging::LogFA SRT_LOGFA_APP = 10;

static size_t g_buffer_size = 1024 * 1024;  // !MB
static bool g_skip_flushing = false;

using namespace std;


void CheckURIParser(const string &uri)
{
    UriParser parser(uri);
    cout << "PARSING URL: " << uri << endl;
    cerr << "SCHEME INDEX: " << int(parser.type()) << endl;
    cout << "PROTOCOL: " << parser.proto() << endl;
    cout << "HOST: " << parser.host() << endl;
    cout << "PORT: " << parser.portno() << endl;
    cout << "PATH: " << parser.path() << endl;
    cout << "PARAMETERS:\n";
    for (auto p : parser.parameters())
        cout << "\t" << p.first << " = " << p.second << endl;
    cout << endl;
}



void TestURIParser()
{
    CheckURIParser(string("file://folder_name/"));
    CheckURIParser(string("file://folder_name/file_name"));
    CheckURIParser(string("file://file_name"));
    CheckURIParser(string("."));
    CheckURIParser(string("./"));
    CheckURIParser(string("./folder"));
    CheckURIParser(string("file://./folder"));
    CheckURIParser(string("C:\\folder\\"));
    CheckURIParser(string("C://folder/"));
}


int main(int argc, char** argv)
{
    set<string>
        o_loglevel = { "ll", "loglevel" },
        o_buffer   = { "b", "buffer" },
        o_verbose  = { "v", "verbose" },
        o_noflush  = { "s", "skipflush" };

    // Options that expect no arguments (ARG_NONE) need not be mentioned.
    vector<OptionScheme> optargs = {
        { o_loglevel, OptionScheme::ARG_ONE },
        { o_buffer, OptionScheme::ARG_ONE }
    };
    options_t params = ProcessOptions(argv, argc, optargs);

    /*
    cerr << "OPTIONS (DEBUG)\n";
    for (auto o: params)
    {
        cerr << "[" << o.first << "] ";
        copy(o.second.begin(), o.second.end(), ostream_iterator<string>(cerr, " "));
        cerr << endl;
    }
    */

    vector<string> args = params[""];
    if (args.size() < 2)
    {
        cerr << "Usage: " << argv[0] << " <source> <target>\n";
        cerr << "Example (receiver):\n   srt_test_folder srt://:4200 ./\n";
        cerr << "      will receive the streaming on port 4200 of the localhost and write to the current folder.\n";
        cerr << "Example (sender):\n   srt_test_folder ./folder_to_send srt://192.168.0.102:4200\n";
        cerr << "      will send the contents of the folder_to_send on port 4200 of the 192.168.0.102.\n";
        cerr << "Example (sender with bandwidth limit):\n   srt_test_folder ./folder_to_send srt://192.168.0.102:4200?maxbw=625000\n";
        cerr << "      will send the contents of the folder_to_send on port 4200 of the 192.168.0.102\n";
        cerr << "      with a bitrate limit of 5 Mbps (625000 bytes/s).\n";

        return 1;
    }

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    UDT::addlogfa(SRT_LOGFA_APP);

    string verbo = Option<OutString>(params, "no", o_verbose);
    if (verbo == "" || !false_names.count(verbo))
        Verbose::on = true;

    string bs = Option<OutString>(params, "", o_buffer);
    if (bs != "")
    {
        ::g_buffer_size = stoi(bs);
    }

    string sf = Option<OutString>(params, "no", o_noflush);
    if (sf == "" || !false_names.count(sf))
        ::g_skip_flushing = true;

    string source = args[0];
    string target = args[1];

    UriParser us(source), ut(target);

    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

    try
    {
        if (us.scheme() == "srt")
        {
            if (ut.scheme() != "file")
            {
                cerr << "SRT to FILE should be specified\n";
                return 1;
            }
            Download(us, ut);
        }
        else if (ut.scheme() == "srt")
        {
            if (us.scheme() != "file")
            {
                cerr << "FILE to SRT should be specified\n";
                return 1;
            }
            Upload(ut, us);
        }
        else
        {
            cerr << "SRT URI must be one of given media.\n";
            return 1;
        }
    }
    catch (std::exception& x)
    {
        cerr << "ERROR: " << x.what() << endl;
        return 1;
    }


    return 0;
}

void ExtractPath(string path, ref_t<string> dir, ref_t<string> fname)
{
    string directory = path;
    string filename = "";

    struct stat state;
    stat(path.c_str(), &state);
    if (!S_ISDIR(state.st_mode))
    {
        // Extract directory as a butlast part of path
        size_t pos = path.find_last_of("/");
        if (pos == string::npos)
        {
            filename = path;
            directory = ".";
        }
        else
        {
            directory = path.substr(0, pos);
            filename = path.substr(pos + 1);
        }
    }

    if (directory[0] != '/')
    {
        // Glue in the absolute prefix of the current directory
        // to make it absolute. This is needed to properly interpret
        // the fixed uri.
        static const size_t s_max_path = 4096; // don't care how proper this is
        char tmppath[s_max_path];
        char* gwd = getcwd(tmppath, s_max_path);
        if (!gwd)
        {
            // Don't bother with that now. We need something better for that anyway.
            throw std::invalid_argument("Path too long");
        }
        string wd = gwd;

        directory = wd + "/" + directory;
    }

    *dir = directory;
    *fname = filename;
}


// Returns true on success, false on error
bool CreateFolder(const string &path)
{
#if defined(_WIN32)
    const int status = _mkdir(path.c_str());
#else
    // read/write/search permissions for owner and group,
    // and read/search permissions for others.
    const int status = mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
    if (0 == status)
    {
        Verb() << "Directory '" << path << "' was successfully created\n";
        return true;
    }

    if (EEXIST == errno)
    {
        Verb() << "Directory '" << path << "' exists\n";
        return true;
    }

    Verb() << "Directory '" << path << "' failed to be created\n";
    return false;
}


// Returns true on success, false on error
// TODO: avoid multiple serial delimiters
bool CreateSubfolders(const string &path)
{
    size_t found = path.find("./");
    if (found == std::string::npos)
    {
        found = path.find(".\\");
    }

    size_t pos = found != std::string::npos ? (found + 2) : 0;
    const size_t last_delim = path.find_last_of("/\\");
    if (last_delim == string::npos || last_delim < pos)
    {
        Verb() << "No folders to create\n";
        return true;
    }

    while (pos != std::string::npos && pos != last_delim)
    {
        pos = path.find_first_of("\\/", pos + 1);
        Verb() << "Creating folder " << path.substr(0, pos) << "\n";
        if (!CreateFolder(path.substr(0, pos).c_str()))
            return false;
    };

    return true;
}



bool TransmitFile(const string &filename, const string &upload_name, const SRTSOCKET ss, vector<char> &buf)
{
    ifstream ifile(filename, ios::binary);
    if (!ifile)
    {
        cerr << "Error opening file: '" << filename << "'\n";
        return true;
    }

    const chrono::steady_clock::time_point time_start = chrono::steady_clock::now();
    size_t file_size = 0;

    Verb() << "Transmitting '" << filename << " to " << upload_name;

    /*   1 byte      string    1 byte
     * ------------------------------------------------
     * | ......EF | Filename | 0     | Payload
     * ------------------------------------------------
     * E - enf of file flag
     * F - frist sefment of a file (flag)
     * We add +2 to include the first byte and the \0-character
     */
    int hdr_size = snprintf(buf.data() + 1, buf.size(), "%s", upload_name.c_str()) + 2;

    for (;;)
    {
        const int n = (int) ifile.read(buf.data() + hdr_size, streamsize (buf.size() - hdr_size)).gcount();
        const bool is_eof = ifile.eof();
        const bool is_start = hdr_size > 1;
        buf[0] = (is_eof ? 2 : 0) | (is_start ? 1 : 0);

        size_t shift = 0;
        if (n > 0)
        {
            const int st = srt_send(ss, buf.data() + shift, n + hdr_size);
            file_size += n;

            if (st == SRT_ERROR)
            {
                cerr << "Upload: SRT error: " << srt_getlasterror_str() << endl;
                return false;
            }
            if (st != n + hdr_size) {
                cerr << "Upload error: not full delivery" << endl;
                return false;
            }

            shift += st - hdr_size;
            hdr_size = 1;
        }

        if (is_eof)
            break;

        if (!ifile.good())
        {
            cerr << "ERROR while reading file\n";
            return false;
        }
    }

    const chrono::steady_clock::time_point time_end = chrono::steady_clock::now();
    const auto delta_us = chrono::duration_cast<chrono::microseconds>(time_end - time_start).count();

    const size_t rate_kbps = (file_size * 1000) / (delta_us) * 8;
    Verb() << "--> done (" << file_size / 1024 << " kbytes transfered at " << rate_kbps << " kbps, took "
        << chrono::duration_cast<chrono::seconds>(time_end - time_start).count() << " s";

    return true;
}


bool DoUploadFolderContents(UriParser& ut, string path)
{
    ut["transtype"] = string("file");
    ut["messageapi"] = string("true");
    ut["sndbuf"] = to_string(1061313/*g_buffer_size*/ /* 1456*/);
    SrtModel m(ut.host(), ut.portno(), ut.parameters());

    string dummy;
    m.Establish(Ref(dummy));

    std::list<pair<dirent*, string>> processing_list;

    auto get_files = [&processing_list](const std::string &path)
    {
        struct dirent **files;
        const int n = scandir(path.c_str(), &files, NULL, alphasort);
        if (n < 0)
        {
            cerr << "No files found in the directory: '" << path << "'";
            return false;
        }

        for (int i = 0; i < n; ++i)
        {
            if (0 == strcmp(files[i]->d_name, ".") || 0 == strcmp(files[i]->d_name, ".."))
            {
                free(files[i]);
                continue;
            }

            processing_list.push_back(pair<dirent*, string>(files[i], path + "/"));
        }

        free(files);
        return true;
    };

    /* Initial scan for files in the directory */
    get_files(path);

    // Use a manual loop for reading from SRT
    vector<char> buf(::g_buffer_size);

    while (!processing_list.empty())
    {
        dirent* ent = processing_list.front().first;
        string dir  = processing_list.front().second;
        processing_list.pop_front();

        if (ent->d_type == DT_DIR)
        {
            get_files(dir + ent->d_name);
            free(ent);
            continue;
        }

        if (ent->d_type != DT_REG)
        {
            free(ent);
            continue;
        }

        cerr << "File: '" << dir << ent->d_name << "'\n";
        const bool transmit_res = TransmitFile(dir + ent->d_name, dir + ent->d_name,
            m.Socket(), buf);
        free(ent);

        if (!transmit_res)
            break;
    };

    while (!processing_list.empty())
    {
        dirent* ent = processing_list.front().first;
        processing_list.pop_front();
        free(ent);
    };

    // We have to check if the sending buffer is empty.
    // Or we will loose this data, because SRT is not waiting
    // for all the data to be sent in a general live streaming use case,
    // as it might be not something it is expected to do, and may lead to
    // to unnesessary waits on destroy.
    // srt_getsndbuffer() is designed to handle such cases.
    const SRTSOCKET sock = m.Socket();
    size_t blocks = 0;
    do
    {
        if (SRT_ERROR == srt_getsndbuffer(sock, &blocks, nullptr))
            break;

        if (blocks)
            this_thread::sleep_for(chrono::milliseconds(5));
    } while (blocks != 0);

    return true;
}


#if ENABLE_JSON_INPUT
bool DoUploadJSONSeq(UriParser& ut, string path, const string &dir)
{
#ifndef SRT_EMUL
    ut["transtype"] = string("file");
    ut["messageapi"] = string("true");
    ut["sndbuf"] = to_string(1061313/*g_buffer_size*/ /* 1456*/);
    SrtModel m(ut.host(), ut.portno(), ut.parameters());
#endif


    auto to_system_clock =
        [](const std::string &string_time)
    {
        std::istringstream ss(string_time);
        std::chrono::system_clock::time_point tp;
        ss >> date::parse("%Y-%m-%dT%T", tp);
        return tp;
    };

    std::ifstream frequests("." + path);

    if (!frequests)
    {
        std::cerr << "Error opening file " << path << "\n";
        return false;
    }

    json j;
    frequests >> j;

    const std::chrono::system_clock::time_point start_time
        = to_system_clock(j["recording_started"].get<std::string>());
    chrono::system_clock::time_point time_prev = start_time;

    std::cout << j << endl;

#ifndef SRT_EMUL
    string dummy;
    m.Establish(Ref(dummy));
#endif

    vector<char> buf(::g_buffer_size);
    const chrono::steady_clock::time_point time_start = chrono::steady_clock::now();

    while (frequests)
    {
        try
        {
            frequests >> j;
        }
        catch (json::parse_error &ex)
        {
            // 101 in case of an unexpected token
            // 102 if to_unicode fails or surrogate error
            // 103 if to_unicode fails
            std::cerr << "ERROR " << ex.what() << std::endl;
            break;
        }

        const std::chrono::system_clock::time_point rcv_time
            = to_system_clock(j["metadata"]["receive_completed"].get<std::string>());

        const auto delta_ms = chrono::duration_cast<chrono::milliseconds>(rcv_time - time_prev);
        const auto delta_ms_from_start = chrono::duration_cast<chrono::milliseconds>(rcv_time - start_time);

        const chrono::steady_clock::time_point time_now = chrono::steady_clock::now();
        const auto wait_ms = delta_ms_from_start - chrono::duration_cast<chrono::milliseconds>(time_now - time_start);
        cout << "Waiting for " << wait_ms.count() << " ms" << endl;
        this_thread::sleep_for(wait_ms);

        time_prev = rcv_time;
        const auto hdr = j["metadata"]["headers"];
        Verb() << "Delta = " << delta_ms.count() << " ms (" << delta_ms_from_start.count() << " ms from start, ";
        std::cout << "Content-Length = " << j["metadata"]["headers"][4][1] << ", URI " << j["metadata"]["uri_path"] << ")\n";

        const string src_uri = j["content_filename"];
        const string dst_uri = j["metadata"]["uri_path"];
#ifndef SRT_EMUL
        const bool transmit_res = TransmitFile("." + dir + "/" + src_uri, dst_uri, m.Socket(), buf);

        if (!transmit_res)
            break;
#endif
    }

    return true;
}
#endif


bool DoDownload(UriParser& us, string directory)
{
    us["transtype"] = string("file");
    us["messageapi"] = string("true");
    us["rcvbuf"] = to_string(2 * 1061312 + 1472/*g_buffer_size*/ /* 1456*/);
    SrtModel m(us.host(), us.portno(), us.parameters());

    string dummy;
    m.Establish(Ref(dummy));

    // Disregard the filename, unless the destination file exists.

    string path = directory + "/";
    struct stat state;
    if (stat(path.c_str(), &state) == -1)
    {
        switch (errno)
        {
        case ENOENT:
            // This is expected, go on.
            break;

        default:
            cerr << "Download: error '" << errno << "'when checking destination location: " << path << endl;
            return false;
        }
    }
    else
    {
        // Check if destination is a regular file, if so, allow to overwrite.
        // Otherwise reject.
        if (!S_ISDIR(state.st_mode))
        {
            cerr << "Download: target location '" << path << "' does not designate a folder.\n";
            return false;
        }
    }

    SRTSOCKET ss = m.Socket();
    Verb() << "Downloading from '" << us.uri() << "' to '" << path;

    vector<char> buf(::g_buffer_size);

    chrono::steady_clock::time_point time_start;
    size_t file_size = 0;

    ofstream ofile;
    for (;;)
    {
        const int n = srt_recv(ss, buf.data(), ::g_buffer_size);
        if (n == SRT_ERROR)
        {
            cerr << "Download: SRT error: " << srt_getlasterror_str() << endl;
            return false;
        }

        if (n == 0)
        {
            Verb() << "Nothing was received. Closing.";
            break;
        }

        int hdr_size = 1;
        const bool is_first = (buf[0] & 0x01) != 0;
        const bool is_eof   = (buf[0] & 0x02) != 0;

        if (is_first)
        {
            ofile.close();
            // extranct the filename from the received buffer
            string filename = string(buf.data() + 1);
            hdr_size += filename.size() + 1;    // 1 for null character

            CreateSubfolders(filename);

            ofile.open(filename.c_str(), ios::out | ios::trunc | ios::binary);
            if (!ofile) {
                cerr << "Download: error opening file " << filename << endl;
                break;
            }

            Verb() << "Downloading: --> " << filename;
            time_start = chrono::steady_clock::now();
            file_size = 0;
        }

        if (!ofile)
        {
            cerr << "Download: file is closed while data is received: first packet missed?\n";
            continue;
        }

        ofile.write(buf.data() + hdr_size, n - hdr_size);
        file_size += n - hdr_size;

        if (is_eof)
        {
            ofile.close();
            const chrono::steady_clock::time_point time_end = chrono::steady_clock::now();
            const auto delta_us = chrono::duration_cast<chrono::microseconds>(time_end - time_start).count();

            const size_t rate_kbps = (file_size * 1000) / (delta_us ? delta_us : 1) * 8;
#if ENABLE_JSON_INPUT
            const auto delta_ms = chrono::duration_cast<chrono::milliseconds>(time_end - time_start).count();
            Verb() << "--> done (" << file_size / 1024 << " kbytes transfered at " << rate_kbps << " kbps, took "
                << delta_ms / 1000.0 << " sec)";
#else
            Verb() << "--> done (" << file_size / 1024 << " kbytes transfered at " << rate_kbps << " kbps, took "
                   << chrono::duration_cast<chrono::minutes>(time_end - time_start).count() << " minute(s))";
#endif
        }
    }

    return true;
}


bool Upload(UriParser& srt_target_uri, UriParser& fileuri)
{
    if (fileuri.scheme() != "file")
    {
        cerr << "Upload: source accepted only as a folder\n";
        return false;
    }

    string path = fileuri.path();
    string directory, filename;
    ExtractPath(path, ref(directory), ref(filename));

    if (!filename.empty())
    {
        cerr << "Upload: source accepted only as a folder (file "
             << filename << " is specified)\n";
        return false;
    }

    Verb() << "Extract path '" << path << "': directory=" << directory;

    // Add some extra parameters.
    srt_target_uri["transtype"] = "file";

#if ENABLE_JSON_INPUT
    return DoUploadJSONSeq(srt_target_uri, path, directory);
#else
    return DoUploadFolderContents(srt_target_uri, path);
#endif
}


bool Download(UriParser& srt_source_uri, UriParser& fileuri)
{
    if (fileuri.scheme() != "file")
    {
        cerr << "Download: target accepted only as a folder\n";
        return false;
    }

    string path = fileuri.path(), directory, filename;
    ExtractPath(path, Ref(directory), Ref(filename));

    srt_source_uri["transtype"] = "file";

    return DoDownload(srt_source_uri, directory);
}

