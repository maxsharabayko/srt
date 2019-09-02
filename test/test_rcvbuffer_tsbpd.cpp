#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <numeric>

#include "rcvbuffer.h"

using namespace std;

class TestRcvBufferTSBPD
    : public ::testing::Test
{
protected:
    TestRcvBufferTSBPD()
    {
        // initialization code here
    }

    ~TestRcvBufferTSBPD()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        m_unit_queue = make_unique<CUnitQueue>();
        m_unit_queue->init(m_buff_size_pkts, 1500, AF_INET);
        m_rcv_buffer = make_unique<CRcvBuffer2>(m_init_seqno, m_buff_size_pkts);
        m_rcv_buffer->setTsbPdMode(m_peer_start_time_us, m_delay_us);
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_unit_queue.reset();
        m_rcv_buffer.reset();
    }

protected:

    unique_ptr<CUnitQueue>  m_unit_queue;
    unique_ptr<CRcvBuffer2> m_rcv_buffer;
    const int m_buff_size_pkts = 16;
    const int m_init_seqno = 1000;
    const uint64_t m_peer_start_time_us = 100000;  // now() - HS.timestamp, microseconds
    const uint64_t m_delay_us = 200000; // 200 ms
};




/// TSBPD ON, not acknowledged ready to play packet is preceeded by a missing packet.
/// So the CRcvBuffer2::updateState() function should drop the missing packet.
/// TSBPD mode is ON. The packet has a timestamp of 200 us.
/// The TSBPD delay is set to 200 ms. This means, that the packet can be played
/// not earlier than after 200200 microseconds from the peer start time.
/// The peer start time is set to 100000 us.
///
/// 
/// |<m_iMaxPos>|
/// |          /
/// |        /
/// |       |
/// +---+---+---+---+---+---+   +---+
/// | 0 | 1 | 0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+---+---+---+---+---+   +---+
/// |       |
/// |       \__last pkt received
/// |
/// \___ m_iStartPos: first message to read
///  \___ m_iLastAckPos: last ack sent
///
/// m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
///
TEST_F(TestRcvBufferTSBPD, UnackPreceedsMissing)
{
    // Add a solo packet to position m_init_seqno + 1 with timestamp 200 us
    const int seqno = m_init_seqno + 1;
    const size_t pld_size = 1456;
    CUnit* unit = m_unit_queue->getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_iFlag = CUnit::GOOD;
    CPacket& pkt = unit->m_Packet;
    pkt.setLength(pld_size);
    pkt.m_iSeqNo = seqno;
    pkt.m_iMsgNo |= PacketBoundaryBits(PB_SOLO);
    pkt.m_iTimeStamp = static_cast<int32_t>(200);
    EXPECT_EQ(m_rcv_buffer->insert(unit), 0);

    // Check that getFirstValidPacketInfo() returns first valid packet.
    const auto pkt_info = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(pkt_info.tsbpd_time, m_peer_start_time_us + pkt.m_iTimeStamp + m_delay_us);
    EXPECT_EQ(pkt_info.seqno, seqno);
    EXPECT_TRUE(pkt_info.seq_gap);

    // The packet is not yet acknowledges, so we can't read it
    EXPECT_FALSE(m_rcv_buffer->canRead(m_peer_start_time_us));

    // The packet is preceeded by a gap, so we can't acknowledge it
    EXPECT_FALSE(m_rcv_buffer->canAck());

    // updateState() should drop the missing packet
    const uint64_t now = m_peer_start_time_us + pkt.m_iTimeStamp + m_delay_us + 1;
    m_rcv_buffer->updateState(now);

    // Now the missing packet is droped, so we can acknowledge the existing packet.
    EXPECT_TRUE(m_rcv_buffer->canAck());
}


