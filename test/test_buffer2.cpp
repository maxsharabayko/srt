#include "gtest/gtest.h"
#include <array>
#include <numeric>

#include "rcvbuffer.h"




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
TEST(CRcvBuffer2, HandleSeqGap)
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
    const size_t buf_length = payload_size * message_len_in_pkts;
    std::array<char, buf_length> src_buffer;
    std::iota(src_buffer.begin(), src_buffer.end(), (char)0);

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

        memcpy(packet.m_pcData, src_buffer.data() + i * payload_size, payload_size);

        EXPECT_EQ(rcv_buffer.insert(unit), 0);
        EXPECT_FALSE(rcv_buffer.canRead());

        rcv_buffer.ack(packet.m_iSeqNo + 1);

        EXPECT_EQ(rcv_buffer.canRead(), is_last_packet);
        //EXPECT_EQ(rcv_buffer.countReadable(), is_last_packet ? message_len_in_pkts : 0);
    }

    // Read the whole message from the buffer
    std::array<char, buf_length> read_buffer;
    const int read_len = rcv_buffer.readMessage(read_buffer.data(), buf_length);
    EXPECT_EQ(read_len, payload_size * message_len_in_pkts);
    EXPECT_TRUE(read_buffer == src_buffer);
    EXPECT_FALSE(rcv_buffer.canRead());
}



TEST(CRcvBuffer2, GetFirstValidPacket)
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


/// In this test case one packet is inserted into the CRcvBuffer2.
/// TSBPD mode is ON. The packet has a timestamp of 200 us.
/// The TSBPD delay is set to 200 ms. This means, that the packet can be played
/// not earlier than after 200200 microseconds from the peer start time.
/// The peer start time is set to 100000 us.
TEST(CRcvBuffer2, TsbPdReadMessage)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBuffer2 rcv_buffer(initial_seqno, buffer_size_pkts);


    const uint64_t peer_start_time_us = 100000;  // now() - HS.timestamp, microseconds

    const uint64_t delay_us = 200000; // 200 ms
    rcv_buffer.setTsbPdMode(peer_start_time_us, delay_us, true);

    int seqno = initial_seqno;
    const size_t payload_size = 1456;
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_iFlag = CUnit::GOOD;
    CPacket& pkt = unit->m_Packet;
    pkt.setLength(payload_size);
    pkt.m_iSeqNo = seqno;
    pkt.m_iMsgNo |= PacketBoundaryBits(PB_SOLO);
    pkt.m_iTimeStamp = static_cast<int32_t>(200);
    EXPECT_EQ(rcv_buffer.insert(unit), 0);


    const auto pkt_info = rcv_buffer.getFirstValidPacketInfo();

    std::cout << "pkt time: " << pkt_info.tsbpd_time << std::endl;
    EXPECT_EQ(pkt_info.tsbpd_time, peer_start_time_us + pkt.m_iTimeStamp + delay_us);

    // The packet is not yet acknowledges, so we can't read it
    EXPECT_FALSE(rcv_buffer.canRead(peer_start_time_us));

    rcv_buffer.ack(CSeqNo::incseq(seqno));
    // Expect it is not time to read the next packet
    EXPECT_FALSE(rcv_buffer.canRead(peer_start_time_us));
    EXPECT_FALSE(rcv_buffer.canRead(peer_start_time_us + delay_us));
    EXPECT_TRUE(rcv_buffer.canRead(peer_start_time_us + pkt.m_iTimeStamp + delay_us));
    EXPECT_TRUE(rcv_buffer.canRead(peer_start_time_us + pkt.m_iTimeStamp + delay_us + 1));
}



/// TSBPD ON, not acknowledged ready to play packet is preceeded by a missing packet.
/// So the CRcvBuffer2::updateState() function should drop the missing packet.
/// TSBPD mode is ON. The packet has a timestamp of 200 us.
/// The TSBPD delay is set to 200 ms. This means, that the packet can be played
/// not earlier than after 200200 microseconds from the peer start time.
/// The peer start time is set to 100000 us.
TEST(CRcvBuffer2, TsbPdReadMessage2)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBuffer2 rcv_buffer(initial_seqno, buffer_size_pkts);


    const uint64_t peer_start_time_us = 100000;  // now() - HS.timestamp, microseconds

    const uint64_t delay_us = 200000; // 200 ms
    rcv_buffer.setTsbPdMode(peer_start_time_us, delay_us, true);

    int seqno = initial_seqno;
    const size_t payload_size = 1456;
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_iFlag = CUnit::GOOD;
    CPacket& pkt = unit->m_Packet;
    pkt.setLength(payload_size);
    pkt.m_iSeqNo = seqno;
    pkt.m_iMsgNo |= PacketBoundaryBits(PB_SOLO);
    pkt.m_iTimeStamp = static_cast<int32_t>(200);
    EXPECT_EQ(rcv_buffer.insert(unit), 0);


    const auto pkt_info = rcv_buffer.getFirstValidPacketInfo();

    std::cout << "pkt time: " << pkt_info.tsbpd_time << std::endl;
    EXPECT_EQ(pkt_info.tsbpd_time, peer_start_time_us + pkt.m_iTimeStamp + delay_us);

    // The packet is not yet acknowledges, so we can't read it
    EXPECT_FALSE(rcv_buffer.canRead(peer_start_time_us));

    rcv_buffer.ack(CSeqNo::incseq(seqno));
    // Expect it is not time to read the next packet
    EXPECT_FALSE(rcv_buffer.canRead(peer_start_time_us));
    EXPECT_FALSE(rcv_buffer.canRead(peer_start_time_us + delay_us));
    EXPECT_TRUE(rcv_buffer.canRead(peer_start_time_us + pkt.m_iTimeStamp + delay_us));
    EXPECT_TRUE(rcv_buffer.canRead(peer_start_time_us + pkt.m_iTimeStamp + delay_us + 1));
}


/// TSBPD mode = ON.
/// A packet is acknowledged and is ready to be read.


/// TSBPD mode = ON.
/// A packet is acknowledged, but not ready to be read
/// In case of blocking RCV call, we can wait directly on the buffer. So no TSBPD thread
/// is needed. However, the SRTO_RCVSYN mode can be turned on in runtime (no protection in setOpt()).
/// In case of non-bocking RCV call, epoll has to be signalled at a certain time.
/// For blocking 



/// TSBPD mode = ON.
/// A packet is not acknowledged, but ready to be read, and has some preceeding missing packet.
/// In that case all missing packets have to be dropped up to the first ready packet. And wait for ACK of that packet.
/// So those missing packets should be removed from the receiver's loss list, and the receiver's buffer
/// has to skip m_iStartPos and m_iLastAckPos up to that packet.


