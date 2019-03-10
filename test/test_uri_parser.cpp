#include "gtest/gtest.h"
#include <string>
#include "uriparser.hpp"

//
// Use https://uriparser.github.io/
// http://www.zedwood.com/article/cpp-boost-url-regex
// http://www.open-std.org/Jtc1/sc22/WG21/docs/papers/2012/n3420.html
// https://stackoverflow.com/questions/2616011/easy-way-to-parse-a-url-in-c-cross-platform
// https://docs.microsoft.com/en-gb/windows/desktop/api/shlwapi/ne-shlwapi-url_scheme
// https://docs.microsoft.com/en-us/windows/desktop/api/shlwapi/nf-shlwapi-parseurla
// https://github.com/chmike/CxxUrl
// https://github.com/uriparser/uriparser/blob/master/test/test.cpp
// https://github.com/cpp-netlib/uri
//
using namespace std;


void PrintURIParser(const UriParser &parser)
{
    cout << "PARSING URL: " << parser.uri() << endl;
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



TEST(UriParser, FileFolder2Slashes)
{
    UriParser parser("file://folder_name/");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("folder_name"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileFolder3Slashes)
{
    UriParser parser("file:///folder_name/");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("folder_name"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileFolderFileName)
{
    UriParser parser("file://folder_name/file_name");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("folder_name/filename"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileName)
{
    UriParser parser("file://file_name");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("filename"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileDot)
{
    UriParser parser(".");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("."));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileDotSlash)
{
    UriParser parser("./");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("./"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileDotFolder)
{
    UriParser parser("./folder");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("./folder"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileScheme2SlashDotFolder)
{
    UriParser parser("file://./folder");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));   // localhost?
    EXPECT_EQ(parser.path(), string("./folder"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileScheme2SlashesLocalhostFolder)
{
    UriParser parser("file://localhost/etc/fstab");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string("localhost"));
    EXPECT_EQ(parser.path(), string("./folder"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileScheme3SlashesFolder)
{
    UriParser parser("file:///etc/fstab");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));
    EXPECT_EQ(parser.path(), string("etc/folder"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileScheme3SlashesWinPath)
{
    UriParser parser("file:///c:/WINDOWS/clock.avi");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));
    EXPECT_EQ(parser.path(), string("c:/WINDOWS/clock.avi"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileScheme2SlashesWinPath)
{
    UriParser parser("file://c:/WINDOWS/clock.avi");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string(""));
    EXPECT_EQ(parser.path(), string("c:/WINDOWS/clock.avi"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


TEST(UriParser, FileScheme2SlashesLocalhostWinPath)
{
    UriParser parser("file://localhost/c:/WINDOWS/clock.avi");
    EXPECT_EQ(parser.type(), UriParser::Type::FILE);
    EXPECT_EQ(parser.proto(), string("file"));
    EXPECT_EQ(parser.host(), string("localhost"));
    EXPECT_EQ(parser.path(), string("c:/WINDOWS/clock.avi"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}


//=============================================================================
// SCHEME: SRT
//=============================================================================

TEST(UriParser, SRTSchemeIPPortParams)
{
    UriParser parser("srt://192.168.0.1:4200?mode=caller&maxbw=125000");
    EXPECT_EQ(parser.type(), UriParser::Type::SRT);
    EXPECT_EQ(parser.proto(), string("srt"));
    EXPECT_EQ(parser.host(), string("192.168.0.1"));
    EXPECT_EQ(parser.portno(), 4200);
    EXPECT_EQ(parser.path(), string(""));
    EXPECT_EQ(parser.parameters().size(), 2);

}


TEST(UriParser, SRTSchemeUppercaseIPPortParams)
{
    UriParser parser("SRT://192.168.0.1:4200?mode=caller&maxbw=125000");
    EXPECT_EQ(parser.type(), UriParser::Type::SRT);
    EXPECT_EQ(parser.proto(), string("srt"));
    EXPECT_EQ(parser.host(), string(""));
    EXPECT_EQ(parser.path(), string("c:/WINDOWS/clock.avi"));
    EXPECT_EQ(parser.portno(), 0);
    EXPECT_EQ(parser.parameters().empty(), true);
}

