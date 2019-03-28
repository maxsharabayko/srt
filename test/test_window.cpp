#include "gtest/gtest.h"
#include "window.h"
#include <vector>


using namespace std;


TEST(PktTimeWindowTools, getPktRcvSpeed_in)
{
    vector<int> window = { 69, 6, 5, 5, 5, 4, 5, 5, 7, 5, 5, 5, 5, 5, 6, 6 };
    //vector<int> window = { 5, 5, 5, 5, 176, 8, 4, 6, 5, 5, 5, 493, 9, 5, 5, 5 };
    vector<int> replic(window.size());
    vector<int> bytes(window.size(), 0);

    int bytesps = 0;
    const int rcv_pktsps = 
        CPktTimeWindowTools::getPktRcvSpeed_in(window.data(), replic.data(),
            bytes.data(), window.size(), bytesps);

    EXPECT_EQ(rcv_pktsps, 200000);
}
