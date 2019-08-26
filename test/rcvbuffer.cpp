#include "rcvbuffer.h"



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




    CRcvBuffer2::CRcvBuffer2(int initSeqNo, size_t size)
        : m_size(size)
        , m_iLastAckSeqNo(initSeqNo)
        , m_pUnit(NULL)
        , m_iStartPos(0)
        , m_iLastAckPos(0)
        , m_iFirstUnreadablePos(0)
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

    CRcvBuffer2::~CRcvBuffer2()
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


    /// Insert a unit into the buffer.
    /// Similar to CRcvBuffer::addData(CUnit* unit, int offset)
    ///
    /// @param [in] unit pointer to a data unit containing new packet
    /// @param [in] offset offset from last ACK point.
    ///
    /// @return  0 on success, -1 if packet is already in buffer, -2 if packet is before m_iLastAckSeqNo.
    int CRcvBuffer2::insert(CUnit* unit)
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

        updateReadablePos();
    }


    /// read a message.
    /// @param [out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    /// @return actuall size of data read.
    int CRcvBuffer2::readMessage(char* data, int len)
    {
        return 0;
    }



    /// Query how many buffer space is left for data receiving.
    ///
    /// @return size of available buffer space (including user buffer) for data receiving.
    int CRcvBuffer2::getAvailBufSize() const
    {
        // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
        return m_size - getRcvDataSize() - 1;
    }

    /// Query how many data has been continuously received (for reading) and ready to play (tsbpdtime < now).
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
    /// @return size of valid (continous) data for reading.
    int CRcvBuffer2::getRcvDataSize() const
    {
        if (m_iLastAckPos >= m_iStartPos)
            return m_iLastAckPos - m_iStartPos;

        return m_size + m_iLastAckPos - m_iStartPos;
    }


    size_t CRcvBuffer2::countReadable() const
    {
        if (m_iFirstUnreadablePos >= m_iStartPos)
            return m_iFirstUnreadablePos - m_iStartPos;
        return m_size + m_iFirstUnreadablePos - m_iStartPos;
    }


    bool CRcvBuffer2::canRead() const
    {
        return (m_iFirstUnreadablePos != m_iStartPos);
    }



    void CRcvBuffer2::countBytes(int pkts, int bytes, bool acked)
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


    void CRcvBuffer2::updateReadablePos()
    {
        //const PacketBoundary boundary = packet.getMsgBoundary();

        //// The simplest case is when inserting a sequential PB_SOLO packet.
        //if (boundary == PB_SOLO && (m_iFirstUnreadablePos + 1) % m_size == pos)
        //{
        //    m_iFirstUnreadablePos = pos;
        //    return;
        //}

        // Check if the gap is filled.
        SRT_ASSERT(m_pUnit[m_iFirstUnreadablePos]);

        int pos = m_iFirstUnreadablePos;
        while (m_pUnit[pos]->m_iFlag == CUnit::GOOD
            && m_pUnit[pos]->m_Packet.getMsgBoundary() & PB_FIRST)
        {
            bool good = true;

            // look ahead for the whole message

            // We expect to see either of:
            // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST]
            // [PB_SOLO]
            // but not:
            // [PB_FIRST] NULL ...
            // [PB_FIRST] FREE/PASSACK/DROPPED...
            // If the message didn't look as expected, interrupt this.

            // This begins with a message starting at m_iStartPos
            // up to m_iLastAckPos OR until the PB_LAST message is found.
            // If any of the units on this way isn't good, this OUTER loop
            // will be interrupted.
            for (int i = pos; i != m_iLastAckPos; i = (i + 1) % m_size)
            {
                if (!m_pUnit[i] || m_pUnit[i]->m_iFlag != CUnit::GOOD)
                {
                    good = false;
                    break;
                }

                // Likewise, boundary() & PB_LAST will be satisfied for last OR solo.
                if (m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST)
                {
                    m_iFirstUnreadablePos = (i + 1) % m_size;
                    break;
                }
            }

            if (pos == m_iFirstUnreadablePos || !m_pUnit[m_iFirstUnreadablePos])
                break;

            pos = m_iFirstUnreadablePos;
        }


        // 1. If there is a gap between this packet and m_iLastReadablePos
        // then no sense to update m_iLastReadablePos.

        // 2. The simplest case is when this is the first sequntial packet
    }


    void CRcvBuffer2::findMessage()
    {

    }




