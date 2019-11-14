#include "gtest/gtest.h"
#include <array>
#include <numeric>

#include "rcvbuffer.h"

using namespace std;


/*!
 * This set of tests has the following CRcvBuffer2 common configuration:
 * - TSBPD ode = OFF
 * 
 */



class TestRcvBuffer2Read
    : public ::testing::Test
{
protected:
    TestRcvBuffer2Read()
    {
        // initialization code here
    }

    ~TestRcvBuffer2Read()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // Constructs CRcvBuffer2 and CUnitQueue
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14
        m_unit_queue = unique_ptr<CUnitQueue>(new CUnitQueue);
        m_unit_queue->init(m_buff_size_pkts, 1500, AF_INET);
        m_rcv_buffer = unique_ptr<CRcvBuffer2>(new CRcvBuffer2(m_init_seqno, m_buff_size_pkts));
    }

    // Destructs CRcvBuffer2 and CUnitQueue
    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_unit_queue.reset();
        m_rcv_buffer.reset();
    }

public:

    void addMessage(size_t msg_len_pkts, int start_seqno, bool out_of_order = false)
    {
        for (size_t i = 0; i < msg_len_pkts; ++i)
        {
            CUnit* unit = m_unit_queue->getNextAvailUnit();
            EXPECT_NE(unit, nullptr);

            CPacket& packet = unit->m_Packet;
            packet.setLength(m_payload_sz);
            packet.m_iSeqNo = start_seqno + i;
            packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
            if (i == 0)
                packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
            const bool is_last_packet = (i == (msg_len_pkts - 1));
            if (is_last_packet)
                packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);

            if (!out_of_order)
            {
                packet.m_iMsgNo |= MSGNO_PACKET_INORDER::wrap(1);
                EXPECT_TRUE(packet.getMsgOrderFlag());
            }

            m_unit_queue->makeUnitGood(unit);
            EXPECT_EQ(m_rcv_buffer->insert(unit), 0);
            // TODOL checkPacketPos(unit);
        }
    }

    void checkPacketPos(CUnit* unit)
    {
        // TODO: check that a certain packet was placed into the right
        // position with right offset.

        // m_rcv_buffer->peek(offset) == unit;
    }

protected:

    unique_ptr<CUnitQueue>  m_unit_queue;
    unique_ptr<CRcvBuffer2> m_rcv_buffer;
    const int m_buff_size_pkts = 16;
    const int m_init_seqno = 1000;
    static const size_t m_payload_sz = 1456;
};


/// One packet is added to the buffer. Should be read after ACK.
/// 1. insert
///   /
/// +---+  ---+---+---+---+---+   +---+
/// | 1 |   0 | 0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+  ---+---+---+---+---+   +---+
///   \
/// 2. ack
/// 3. read
///
TEST_F(TestRcvBuffer2Read, OnePacket)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->canRead());

    int read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, -1);

    // Full ACK
    m_rcv_buffer->ack(m_init_seqno + 1);

    EXPECT_TRUE(m_rcv_buffer->canRead());

    read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, msg_bytelen);
}

/// One packet is added to the buffer after 1-packet gap. Should be read after ACK.
/// 1. insert
///       |
/// +---+---+  ---+---+---+---+   +---+
/// | 0 | 1 |   0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+---+  ---+---+---+---+   +---+
///   \    \
/// 2. ack  |
/// 3. ack -^
/// 4. read
///
TEST_F(TestRcvBuffer2Read, OnePacketAfterGap)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno + 1, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->canRead());

    int read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, -1);

    // ACK first missing packet
    m_rcv_buffer->ack(m_init_seqno + 1);

    EXPECT_FALSE(m_rcv_buffer->canRead());

    read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, 0);

    m_rcv_buffer->ack(m_init_seqno + 2);
    EXPECT_TRUE(m_rcv_buffer->canRead());

    read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, msg_bytelen);
}

#if 0
TEST_F(TestRcvBuffer2Read, MsgAcked)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Half ACK
    m_rcv_buffer->ackData(2);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Full ACK
    m_rcv_buffer->ackData(2);

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);
}


TEST_F(TestRcvBuffer2Read, MsgHalfAck)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    m_rcv_buffer->ackData(2);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    const int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);
}


TEST_F(TestRcvBuffer2Read, OutOfOrderMsgNoACK)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    const int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
}
#endif
