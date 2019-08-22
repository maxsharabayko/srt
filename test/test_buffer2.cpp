#include "gtest/gtest.h"

#include "buffer2.h"




TEST(CRcvBuffer2, Create)
{
    const int buffer_size = 65536;
    CRcvBuffer2 rcv_buffer(0, buffer_size);

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size - 1);   // logic
}



TEST(CRcvBuffer2, FullBuffer)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBuffer2 rcv_buffer(initial_seqno, buffer_size_pkts);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < rcv_buffer.getAvailBufSize(); ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        unit->m_Packet.m_iSeqNo = initial_seqno + i;
        EXPECT_EQ(rcv_buffer.insert(unit), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);   // logic

    // Try to add a unit with sequence number that already exsists in the buffer.
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_Packet.setLength(payload_size);
    unit->m_Packet.m_iSeqNo = initial_seqno;

    EXPECT_EQ(rcv_buffer.insert(unit), -1);

    // Acknowledge data
    rcv_buffer.ack(initial_seqno + buffer_size_pkts - 1);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), 0);

    // Try to add a unit with sequence number that was already acknowledged.
    unit->m_Packet.setLength(payload_size);
    unit->m_Packet.m_iSeqNo = initial_seqno;

    EXPECT_EQ(rcv_buffer.insert(unit), -2);


    // Try to add more data than the available size of the buffer
    //CUnit* unit = unit_queue.getNextAvailUnit();
    //EXPECT_NE(unit, nullptr);
    //EXPECT_EQ(rcv_buffer.add(unit), -1);

    //std::array<char, payload_size> buff;
    //for (int i = 0; i < buffer_size_pkts - 1; ++i)
    //{
    //	const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
    //	EXPECT_EQ(res, payload_size);
    //}
}

/// In this test case two packets are inserted in the CRcvBuffer2,
/// but with a gap in sequence numbers. The test checks that this situation
/// is handled properly.
TEST(CRcvBuffer2, HandleSeqNoGap)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBuffer2 rcv_buffer(initial_seqno, buffer_size_pkts);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < 2; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        unit->m_Packet.m_iSeqNo = initial_seqno + i;
        EXPECT_EQ(rcv_buffer.insert(unit), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);   // logic

}


/// In this test case several units are inserted in the CRcvBuffer
/// but all composing a one message. In details, each packet [  ]
/// [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENsT] [PB_LAST]
///
TEST(CRcvBuffer2, OneMessageInSeveralPackets)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1000;
    CRcvBuffer2 rcv_buffer(initial_seqno, buffer_size_pkts);

    const size_t payload_size = 1456;
    const int message_len_in_pkts = 4;
    for (int i = 0; i < message_len_in_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_iFlag = CUnit::GOOD;
        CPacket& packet = unit->m_Packet;
        packet.setLength(payload_size);
        packet.m_iSeqNo = initial_seqno + i;

        packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
        if (i == 0)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
        const bool is_last_packet = (i == message_len_in_pkts - 1);
        if (is_last_packet)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);

        EXPECT_EQ(rcv_buffer.insert(unit), 0);
        EXPECT_EQ(rcv_buffer.canRead(), false);

        rcv_buffer.ack(packet.m_iSeqNo + 1);

        EXPECT_EQ(rcv_buffer.canRead(), is_last_packet);
        //EXPECT_EQ(rcv_buffer.countReadable(), is_last_packet ? message_len_in_pkts : 0);
    }

}




