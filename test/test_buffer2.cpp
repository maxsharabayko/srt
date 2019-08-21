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

