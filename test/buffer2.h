#pragma once

#include "common.h"
#include "queue.h"


namespace srt
{

	template <class ArgType>
	class EventSlotBase
	{
	public:
		virtual void emit(ETransmissionEvent tev, ArgType var) = 0;
		virtual ~EventSlotBase() {}
	};

	template <class ArgType>
	class SimpleEventSlot : public EventSlotBase<ArgType>
	{
	public:
		typedef void dispatcher_t(void* opaque, ETransmissionEvent tev, ArgType var);
		void* opaque;
		dispatcher_t* dispatcher;

		SimpleEventSlot(void* op, dispatcher_t* disp) : opaque(op), dispatcher(disp) {}

		void emit(ETransmissionEvent tev, ArgType var) ATR_OVERRIDE
		{
			(*dispatcher)(opaque, tev, var);
		}
	};

	template <class Class, class ArgType>
	class ObjectEventSlot : public EventSlotBase<ArgType>
	{
	public:
		typedef void (Class::* method_ptr_t)(ETransmissionEvent tev, ArgType var);

		method_ptr_t pm;
		Class* po;

		ObjectEventSlot(Class* o, method_ptr_t m) : pm(m), po(o) {}

		void emit(ETransmissionEvent tev, ArgType var) ATR_OVERRIDE
		{
			(po->*pm)(tev, var);
		}
	};


	template <class ArgType>
	struct EventSlot
	{
		mutable EventSlotBase<ArgType>* slot;
		// Create empty slot. Calls are ignored.
		EventSlot() : slot(0) {}

		// "Stealing" copy constructor, following the auto_ptr method.
		// This isn't very nice, but no other way to do it in C++03
		// without rvalue-reference and move.
		EventSlot(const EventSlot& victim)
		{
			slot = victim.slot; // Should MOVE.
			victim.slot = 0;
		}

		EventSlot(void* op, typename SimpleEventSlot<ArgType>::dispatcher_t* disp)
		{
			slot = new SimpleEventSlot<ArgType>(op, disp);
		}

		template <class ObjectClass>
		EventSlot(ObjectClass* obj, typename ObjectEventSlot<ObjectClass, ArgType>::method_ptr_t method)
		{
			slot = new ObjectEventSlot<ObjectClass, ArgType>(obj, method);
		}

		void emit(ETransmissionEvent tev, ArgType var)
		{
			if (!slot)
				return;
			slot->emit(tev, var);
		}

		~EventSlot()
		{
			if (slot)
				delete slot;
		}
	};

}




//class 


std::vector<EventSlot> m_Slots[TEV__SIZE];





class CBuffer
{

public:

	void add();

	void ack();

	void read();

public:

	bool isDataAvailableToRead();

	bool CanWrite();

	/// Query how many buffer space left for data receiving.
	///
	/// @return size of available buffer space (including user buffer) for data receiving.
	/// 
	size_t getAvailableSize() const;

	

private:

	void OnAvailable();

};




class CSndBuffer2
{

public:

public:

	enum Events
	{
		AVAILABLE_TO_WRITE,
	};

public:



};


//
// TODO: Use enum class if C++11 is available.
//


/*
*   RcvBuffer2 (circular buffer):
*
*   |<------------------- m_iSize ----------------------------->|
*   |       |<--- acked pkts -->|<--- m_iMaxPos --->|           |
*   |       |                   |                   |           |
*   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
*   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
*   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
*             |                 | |               |
*             |                   |               \__last pkt received
*             |                   \___ m_iLastAckPos: last ack sent
*             \___ m_iStartPos: first message to read
*
*   m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
*
*   thread safety:
*    m_iStartPos:   CUDT::m_RecvLock
*    m_iLastAckPos: CUDT::m_AckLock
*    m_iMaxPos:     none? (modified on add and ack
*/


class CRcvBuffer2
{

public:


	CRcvBuffer2(int initSeqNo, size_t size)
		: m_size(size)
		, m_iLastAckSeqNo(initSeqNo)
		, m_pUnit(NULL)
		, m_iStartPos(0)
		, m_iLastAckPos(0)
		, m_iMaxPos(0)
		, m_iNotch(0)
		, m_BytesCountLock()
		, m_iBytesCount(0)
		, m_iAckedPktsCount(0)
		, m_iAckedBytesCount(0)
		, m_iAvgPayloadSz(7 * 188)
	{
		m_pUnit = new CUnit * [m_size];
		for (int i = 0; i < m_size; ++i)
			m_pUnit[i] = NULL;

		pthread_mutex_init(&m_BytesCountLock, NULL);
	}

	~CRcvBuffer2()
	{
		/*for (int i = 0; i < m_size; ++i)
		{
			if (m_pUnit[i] != NULL)
			{
				m_pUnitQueue->makeUnitFree(m_pUnit[i]);
			}
		}*/

		delete[] m_pUnit;

		pthread_mutex_destroy(&m_BytesCountLock);
	}


public:


	/// Insert a unit into the buffer.
	/// Similar to CRcvBuffer::addData(CUnit* unit, int offset)
	///
	/// @param [in] unit pointer to a data unit containing new packet
	/// @param [in] offset offset from last ACK point.
	///
	/// @return  0 on success, -1 if packet is already in buffer, -2 if packet is before m_iLastAckSeqNo.
	int insert(CUnit* unit)
	{
		SRT_ASSERT(unit != NULL);
		const int32_t seqno = unit->m_Packet.getSeqNo();
		const int offset = CSeqNo::seqoff(m_iLastAckSeqNo, seqno);

		if (offset < 0)
			return -2;

		// If >= 2, then probably there is a long gap, and buffer needs to be reset.
		SRT_ASSERT((m_iLastAckPos + offset) / m_size < 2);

		const int pos = (m_iLastAckPos + offset) % m_size;
		if (offset >= m_iMaxPos)
			m_iMaxPos = offset + 1;

		// Packet already exists
		if (m_pUnit[pos] != NULL)
			return -1;

		m_pUnit[pos] = unit;
		countBytes(1, (int)unit->m_Packet.getLength());

		// TODO: Don't want m_pUnitQueue here
		//m_pUnitQueue->makeUnitGood(unit);

		return 0;
	}

	/// Update the ACK point of the buffer.
	/// @param [in] len size of data to be acknowledged.
	/// @return 1 if a user buffer is fulfilled, otherwise 0.
	/// TODO: Should call CTimer::triggerEvent() in the end.
	void CRcvBuffer2::ack(int32_t seqno)
	{
		const int len = CSeqNo::seqoff(m_iLastAckSeqNo, seqno);

		{
			int pkts = 0;
			int bytes = 0;
			const int end = (m_iLastAckPos + len) % m_size;
			for (int i = m_iLastAckPos; i != end; i = (i + 1) % m_size)
			{
				if (m_pUnit[i] == NULL)
					continue;

				pkts++;
				bytes += (int)m_pUnit[i]->m_Packet.getLength();
			}
			if (pkts > 0)
				countBytes(pkts, bytes, true);
		}

		m_iLastAckPos = (m_iLastAckPos + len) % m_size;
		m_iMaxPos -= len;
		if (m_iMaxPos < 0)
			m_iMaxPos = 0;

		m_iLastAckSeqNo = seqno;
	}



	int CRcvBuffer2::read(char* data, int len)
	{
		return 0;
	}


public:

	/// Query how many buffer space is left for data receiving.
	///
	/// @return size of available buffer space (including user buffer) for data receiving.
	int getAvailBufSize() const
	{
		// One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
		return m_size - getRcvDataSize() - 1;
	}

	/// Query how many data has been continuously received (for reading) and ready to play (tsbpdtime < now).
	/// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
	/// @return size of valid (continous) data for reading.
	int getRcvDataSize() const
	{
		if (m_iLastAckPos >= m_iStartPos)
			return m_iLastAckPos - m_iStartPos;

		return m_size + m_iLastAckPos - m_iStartPos;
	}


	int countReadable() const;


private:


	void countBytes(int pkts, int bytes, bool acked = false)
	{
		/*
		* Byte counter changes from both sides (Recv & Ack) of the buffer
		* so the higher level lock is not enough for thread safe op.
		*
		* pkts are...
		*  added (bytes>0, acked=false),
		*  acked (bytes>0, acked=true),
		*  removed (bytes<0, acked=n/a)
		*/
		CGuard cg(m_BytesCountLock);

		if (!acked) //adding new pkt in RcvBuffer
		{
			m_iBytesCount += bytes; /* added or removed bytes from rcv buffer */
			if (bytes > 0) /* Assuming one pkt when adding bytes */
				m_iAvgPayloadSz = avg_iir<100>(m_iAvgPayloadSz, bytes);
		}
		else // acking/removing pkts to/from buffer
		{
			m_iAckedPktsCount += pkts; /* acked or removed pkts from rcv buffer */
			m_iAckedBytesCount += bytes; /* acked or removed bytes from rcv buffer */

			if (bytes < 0)
				m_iBytesCount += bytes; /* removed bytes from rcv buffer */
		}
	}


public:

	enum Events
	{
		AVAILABLE_TO_READ,	// 

	};

public:


private:

	CUnit** m_pUnit;                     // pointer to the buffer
	const size_t m_size;                 // size of the buffer
	CUnitQueue* m_pUnitQueue;            // the shared unit queue

	int m_iLastAckSeqNo;
	int m_iStartPos;                     // the head position for I/O (inclusive)
	int m_iLastAckPos;                   // the last ACKed position (exclusive)
										 // EMPTY: m_iStartPos = m_iLastAckPos   FULL: m_iStartPos = m_iLastAckPos + 1
	int m_iMaxPos;                       // the furthest data position
	int m_iNotch;                        // the starting read point of the first unit

	int m_iBytesCount;                   // Number of payload bytes in the buffer
	int m_iAckedPktsCount;               // Number of acknowledged pkts in the buffer
	int m_iAckedBytesCount;              // Number of acknowledged payload bytes in the buffer
	int m_iAvgPayloadSz;                 // Average payload size for dropped bytes estimation


	pthread_mutex_t m_BytesCountLock;    // used to protect counters operations


};


