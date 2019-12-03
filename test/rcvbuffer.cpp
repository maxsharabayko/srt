#include "rcvbuffer.h"
#include "logging.h"

using namespace srt_logging;
namespace srt_logging
{
    extern Logger mglog;
}
#define rbuflog mglog

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
    : m_pUnit(NULL)
    , m_size(size)
    , m_pUnitQueue(NULL)
    , m_iLastAckSeqNo(initSeqNo)
    , m_iStartPos(0)
    , m_iLastAckPos(0)
    , m_iFirstUnreadablePos(0)
    , m_iMaxPos(0)
    , m_iNotch(0)
    , m_numOutOfOrderPackets(0)
    , m_iFirstReadableOutOfOrder(-1)
    , m_bTLPktDrop(false)
    , m_bTsbPdMode(false)
    , m_uTsbPdDelay(0)
    , m_ullTsbPdTimeBase(0)
    , m_bTsbPdWrapCheck(false)
    , m_BytesCountLock()
    , m_iBytesCount(0)
    , m_iAckedPktsCount(0)
    , m_iAckedBytesCount(0)
    , m_iAvgPayloadSz(7 * 188)
{
    SRT_ASSERT(size < INT_MAX); // All position pointers are integers
    m_pUnit = new CUnit *[m_size];
    for (size_t i = 0; i < m_size; ++i)
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

int CRcvBuffer2::insert(CUnit *unit)
{
    SRT_ASSERT(unit != NULL);
    const int32_t seqno  = unit->m_Packet.getSeqNo();
    const int     offset = CSeqNo::seqoff(m_iLastAckSeqNo, seqno);

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

    // If packet "in order" flag is zero, it can be read out of order
    if (!m_bTsbPdMode && !unit->m_Packet.getMsgOrderFlag())
    {
        ++m_numOutOfOrderPackets;
        onInsertNotInOrderPacket(pos);
    }

    // TODO: Don't want m_pUnitQueue here
    // m_pUnitQueue->makeUnitGood(unit);

    return 0;
}

/// TODO: Should call CTimer::triggerEvent() in the end.
void CRcvBuffer2::ack(int32_t seqno)
{
    SRT_ASSERT(canAck());   // Sanity check only for debug build

    const int len = CSeqNo::seqoff(m_iLastAckSeqNo, seqno);

    SRT_ASSERT(len > 0);
    if (len <= 0)
        return;

    {
        int       pkts  = 0;
        int       bytes = 0;
        const int end   = (m_iLastAckPos + len) % m_size;
        for (int i = m_iLastAckPos; i != end; i = incPos(i))
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

void CRcvBuffer2::dropMissing(int32_t seqno)
{
    // Can drop only when nothing to read, and 
    // first unacknowledged packet is missing.
    SRT_ASSERT(m_iLastAckPos == m_iFirstUnreadablePos);
    SRT_ASSERT(m_iLastAckPos == m_iStartPos);

    int len = CSeqNo::seqoff(m_iLastAckSeqNo, seqno);
    SRT_ASSERT(len > 0);
    if (len <= 0)
        return;

    m_iMaxPos -= len;
    if (m_iMaxPos < 0)
        m_iMaxPos = 0;

    // Check that all packets being dropped are missing.
    while (len > 0)
    {
        SRT_ASSERT(m_pUnit[m_iStartPos] == nullptr);
        m_iStartPos = incPos(m_iStartPos);
        --len;
    }

    // Update positions
    m_iLastAckPos   = m_iStartPos;
    m_iLastAckSeqNo = seqno;
    m_iFirstUnreadablePos = m_iStartPos;
}

int CRcvBuffer2::readMessage(char *data, size_t len)
{
    const bool readAcknowledged = hasReadableAckPkts();
    if (!readAcknowledged && m_iFirstReadableOutOfOrder < 0)
    {
        LOGC(rbuflog.Error, log << "CRcvBuffer2.readMessage(): nothing to read. Ignored canRead() result?");
        return -1;
    }

    const int readPos = readAcknowledged ? m_iStartPos : m_iFirstReadableOutOfOrder;

    size_t remain     = len;
    char * dst        = data;
    int    pkts_read  = 0;
    int    bytes_read = 0;
    bool updateStartPos = (readPos == m_iStartPos);
    for (int i = readPos;; i = incPos(i))
    {
        SRT_ASSERT(m_pUnit[i]);
        if (!m_pUnit[i])
        {
            LOGC(rbuflog.Error, log << "CRcvBuffer2::readMessage(): null packet encountered.");
            break;
        }

        const CPacket &packet  = m_pUnit[i]->m_Packet;
        const size_t   pktsize = packet.getLength();

        const size_t unitsize = std::min(remain, pktsize);
        memcpy(dst, packet.m_pcData, unitsize);
        dst += unitsize;

        ++pkts_read;
        bytes_read += pktsize;

        if (m_bTsbPdMode)
            updateTsbPdTimeBase(packet.getMsgTimeStamp());

        if (m_numOutOfOrderPackets && ~packet.getMsgOrderFlag())
            --m_numOutOfOrderPackets;

        // TODO: make unit free
        // m_pUnitQueue->makeUnitFree(m_pUnit[i]);
        m_pUnit[i] = NULL;

        if (updateStartPos)
        {
            if (i == m_iLastAckPos)
                updateStartPos = false;
            else
                m_iStartPos = i;
        }

        if (packet.getMsgBoundary() & PB_LAST)
        {
            if (readPos == m_iFirstReadableOutOfOrder)
                m_iFirstReadableOutOfOrder = -1;
            break;
        }
    }

    countBytes(-pkts_read, -bytes_read, true);
    updateFirstReadableOutOfOrder();

    return (dst - data);
}

int CRcvBuffer2::getAvailBufSize() const
{
    // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
    return m_size - getRcvDataSize() - 1;
}

int CRcvBuffer2::getRcvDataSize() const
{
    if (m_iLastAckPos >= m_iStartPos)
        return m_iLastAckPos - m_iStartPos;

    return m_size + m_iLastAckPos - m_iStartPos;
}

CRcvBuffer2::PacketInfo CRcvBuffer2::getFirstValidPacketInfo() const
{
    bool      acknowledged = true;
    const int end_pos      = (m_iLastAckPos + m_iMaxPos) % m_size;
    for (int i = m_iStartPos; i != end_pos; i = incPos(i))
    {
        if (i == m_iLastAckPos)
            acknowledged = false;

        if (!m_pUnit[i])
            continue;

        const CPacket &  packet = m_pUnit[i]->m_Packet;
        const PacketInfo info   = {packet.getSeqNo(), acknowledged, i != m_iStartPos, getPktTsbPdTime(packet.getMsgTimeStamp())};
        return info;
    }

    return PacketInfo();
}

bool CRcvBuffer2::canAck() const
{
    if (m_iMaxPos == 0)
        return false;

    return m_pUnit[m_iLastAckPos] != NULL;
}

size_t CRcvBuffer2::countReadable() const
{
    if (m_iFirstUnreadablePos >= m_iStartPos)
        return m_iFirstUnreadablePos - m_iStartPos;
    return m_size + m_iFirstUnreadablePos - m_iStartPos;
}

bool CRcvBuffer2::canRead(uint64_t time_now) const
{
    const bool haveAckedPackets = hasReadableAckPkts();
    if (!m_bTsbPdMode)
    {
        if (haveAckedPackets)
            return true;

        return (m_numOutOfOrderPackets > 0 && m_iFirstReadableOutOfOrder != -1);
    }

    if (!haveAckedPackets)
        return false;

    const auto info = getFirstValidPacketInfo();

    return info.tsbpd_time <= time_now;
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

    if (!acked) // adding new pkt in RcvBuffer
    {
        m_iBytesCount += bytes; /* added or removed bytes from rcv buffer */
        if (bytes > 0)          /* Assuming one pkt when adding bytes */
            m_iAvgPayloadSz = avg_iir<100>(m_iAvgPayloadSz, bytes);
    }
    else // acking/removing pkts to/from buffer
    {
        m_iAckedPktsCount += pkts;   /* acked or removed pkts from rcv buffer */
        m_iAckedBytesCount += bytes; /* acked or removed bytes from rcv buffer */

        if (bytes < 0)
            m_iBytesCount += bytes; /* removed bytes from rcv buffer */
    }
}

void CRcvBuffer2::updateReadablePos()
{
    // const PacketBoundary boundary = packet.getMsgBoundary();

    //// The simplest case is when inserting a sequential PB_SOLO packet.
    // if (boundary == PB_SOLO && (m_iFirstUnreadablePos + 1) % m_size == pos)
    //{
    //    m_iFirstUnreadablePos = pos;
    //    return;
    //}

    // Check if the gap is filled.
    SRT_ASSERT(m_pUnit[m_iFirstUnreadablePos]);

    int pos = m_iFirstUnreadablePos;
    while (m_pUnit[pos]->m_iFlag == CUnit::GOOD && (m_pUnit[pos]->m_Packet.getMsgBoundary() & PB_FIRST))
    {
        // bool good = true;

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
                // good = false;
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

int CRcvBuffer2::findLastMessagePkt()
{
    for (int i = m_iStartPos; i != m_iFirstUnreadablePos; i = incPos(i))
    {
        SRT_ASSERT(m_pUnit[i]);

        if (m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST)
        {
            return i;
        }
    }

    return -1;
}


void CRcvBuffer2::onInsertNotInOrderPacket(int insertPos)
{
    if (m_numOutOfOrderPackets == 0)
        return;

    // If the following condition is true, there is alreadt a packet,
    // that can be read out of order. We don't need to search for
    // another one. The search should be done when that packet is read out from the buffer.
    //
    // There might happen that the packet being added precedes the previously found one.
    // However, it is allowed to re bead out of order, so no need to update the position.
    if (m_iFirstReadableOutOfOrder >= 0)
        return;

    // Just a sanity check. This function is called when a new packet is added.
    // So the should be unacknowledged packets.
    SRT_ASSERT(m_iMaxPos > 0);
    SRT_ASSERT(m_pUnit[insertPos]);
    const CPacket &pkt = m_pUnit[insertPos]->m_Packet;
    const PacketBoundary boundary = pkt.getMsgBoundary();

    //if ((boundary & PB_FIRST) && (boundary & PB_LAST))
    //{
    //    // This packet can be read out of order
    //    m_iFirstReadableOutOfOrder = insertPos;
    //    return;
    //}

    const int msgNo = pkt.getMsgSeq();
    // First check last packet, because it is expected to be received last.
    const bool hasLast = (boundary & PB_LAST) || (-1 < scanNotInOrderMessageRight(insertPos, msgNo));
    if (!hasLast)
        return;

    const int firstPktPos = (boundary & PB_FIRST)
        ? insertPos
        : scanNotInOrderMessageLeft(insertPos, msgNo);
    if (firstPktPos < 0)
        return;

    m_iFirstReadableOutOfOrder = firstPktPos;
    return;
}

void CRcvBuffer2::updateFirstReadableOutOfOrder()
{
    if (hasReadableAckPkts() || m_numOutOfOrderPackets <= 0 || m_iFirstReadableOutOfOrder >= 0)
        return;

    if (m_iMaxPos == 0)
        return;

    int outOfOrderPktsRemain = m_numOutOfOrderPackets;

    // Search further packets to the right.
    // First check if there are packets to the right.
    const int lastPos = (m_iLastAckPos + m_iMaxPos - 1) % m_size;

    int pos = m_iStartPos;
    int posFirst = -1;
    int posLast  = -1;
    int msgNo    = -1;

    for (int pos = m_iStartPos; outOfOrderPktsRemain; pos = incPos(pos))
    {
        if (!m_pUnit[pos])
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        const CPacket& pkt = m_pUnit[pos]->m_Packet;

        if (pkt.getMsgOrderFlag())   // Skip in order packet
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        --outOfOrderPktsRemain;

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
        {
            posFirst = pos;
            msgNo = pkt.getMsgSeq();
        }

        if (pkt.getMsgSeq() != msgNo)
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        if (boundary & PB_LAST)
        {
            m_iFirstReadableOutOfOrder = posFirst;
            return;
        }

        if (pos == lastPos)
            break;
    }

    return;
}

int CRcvBuffer2::scanNotInOrderMessageRight(const int startPos, int msgNo) const
{
    // Search further packets to the right.
    // First check if there are packets to the right.
    const int lastPos = (m_iLastAckPos + m_iMaxPos - 1) % m_size;
    if (startPos == lastPos)
        return -1;

    int pos = startPos;
    do
    {
        pos = incPos(pos);

        if (!m_pUnit[pos])
            break;

        const CPacket& pkt = m_pUnit[pos]->m_Packet;

        if (pkt.getMsgSeq() != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_LAST packet for msgNo " << msgNo);
            return -1;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_LAST)
            return pos;
    } while (pos != lastPos);

    return -1;
}

int CRcvBuffer2::scanNotInOrderMessageLeft(const int startPos, int msgNo) const
{
    // Search preceeding packets to the left.
    // First check if there are packets to the left.
    if (startPos == m_iStartPos)
        return -1;

    int pos = startPos;
    do
    {
        pos = decPos(pos);

        if (!m_pUnit[pos])
            return -1;

        const CPacket& pkt = m_pUnit[pos]->m_Packet;

        if (pkt.getMsgSeq() != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_FIRST packet for msgNo " << msgNo);
            return -1;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
            return pos;
    } while (pos != m_iStartPos);

    return -1;
}


void CRcvBuffer2::setTsbPdMode(uint64_t timebase, uint32_t delay, bool tldrop)
{
    m_bTsbPdMode      = true;
    m_bTLPktDrop      = tldrop;
    m_bTsbPdWrapCheck = false;

    // Timebase passed here comes is calculated as:
    // >>> CTimer::getTime() - ctrlpkt->m_iTimeStamp
    // where ctrlpkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_ullTsbPdTimeBase = timebase;
    // XXX Seems like this may not work correctly.
    // At least this solution this way won't work with application-supplied
    // timestamps. For that case the timestamps should be taken exclusively
    // from the data packets because in case of application-supplied timestamps
    // they come from completely different server and undergo different rules
    // of network latency and drift.
    m_uTsbPdDelay = delay;
}

uint64_t CRcvBuffer2::getTsbPdTimeBase(uint32_t timestamp) const
{
    const uint64_t carryover =
        (m_bTsbPdWrapCheck && timestamp < TSBPD_WRAP_PERIOD) ? uint64_t(CPacket::MAX_TIMESTAMP) + 1 : 0;

    return (m_ullTsbPdTimeBase + carryover);
}

void CRcvBuffer2::updateTsbPdTimeBase(uint32_t timestamp)
{
    /*
     * Packet timestamps wrap around every 01h11m35s (32-bit in usec)
     * When added to the peer start time (base time),
     * wrapped around timestamps don't provide a valid local packet delevery time.
     *
     * A wrap check period starts 30 seconds before the wrap point.
     * In this period, timestamps smaller than 30 seconds are considered to have wrapped around (then adjusted).
     * The wrap check period ends 30 seconds after the wrap point, afterwhich time base has been adjusted.
     */

    // This function should generally return the timebase for the given timestamp.
    // It's assumed that the timestamp, for which this function is being called,
    // is received as monotonic clock. This function then traces the changes in the
    // timestamps passed as argument and catches the moment when the 64-bit timebase
    // should be increased by a "segment length" (MAX_TIMESTAMP+1).

    // The checks will be provided for the following split:
    // [INITIAL30][FOLLOWING30]....[LAST30] <-- == CPacket::MAX_TIMESTAMP
    //
    // The following actions should be taken:
    // 1. Check if this is [LAST30]. If so, ENTER TSBPD-wrap-check state
    // 2. Then, it should turn into [INITIAL30] at some point. If so, use carryover MAX+1.
    // 3. Then it should switch to [FOLLOWING30]. If this is detected,
    //    - EXIT TSBPD-wrap-check state
    //    - save the carryover as the current time base.

    if (m_bTsbPdWrapCheck)
    {
        // Wrap check period.
        if ((timestamp >= TSBPD_WRAP_PERIOD) && (timestamp <= (TSBPD_WRAP_PERIOD * 2)))
        {
            /* Exiting wrap check period (if for packet delivery head) */
            m_bTsbPdWrapCheck = false;
            m_ullTsbPdTimeBase += uint64_t(CPacket::MAX_TIMESTAMP) + 1;
            // tslog.Debug("tsbpd wrap period ends");
        }
        return;
    }

    // Check if timestamp is in the last 30 seconds before reaching the MAX_TIMESTAMP.
    if (timestamp > (CPacket::MAX_TIMESTAMP - TSBPD_WRAP_PERIOD))
    {
        /* Approching wrap around point, start wrap check period (if for packet delivery head) */
        m_bTsbPdWrapCheck = true;
        // tslog.Debug("tsbpd wrap period begins");
    }
}

void CRcvBuffer2::updateState(uint64_t time_now)
{
    if (!m_bTLPktDrop)
        return;

    // Do nothing if there are unread packets.
    if (m_iStartPos != m_iLastAckPos)
        return;

    // Do nothing if there are no unacknowledged packets
    if (m_iMaxPos == 0)
        return;

    // Nothing to drop
    if (m_pUnit[m_iLastAckPos] != NULL)
        return;

    int i;
    const int end_pos = (m_iLastAckPos + m_iMaxPos) % m_size;
    for (i = m_iLastAckPos; i != end_pos; i = incPos(i))
    {
        if (m_pUnit[i])
            break;
    }

    SRT_ASSERT(m_pUnit[i]);

    const CPacket& pkt = m_pUnit[i]->m_Packet;
    if (getPktTsbPdTime(pkt.getMsgTimeStamp()) > time_now)
        return;

    m_iFirstUnreadablePos = i;
    ack(m_pUnit[i]->m_Packet.getSeqNo());
}

uint64_t CRcvBuffer2::getPktTsbPdTime(uint32_t timestamp) const
{
    return (getTsbPdTimeBase(timestamp) + m_uTsbPdDelay + timestamp + m_DriftTracer.drift());
}
