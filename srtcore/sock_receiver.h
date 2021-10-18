#include "srt.h"
#include "common.h"


namespace srt {


class CSRTReceiver
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration duration;

public:
    void setInitialRcvSeq(int32_t isn)
    {
        m_iRcvLastAck = isn;
#ifdef ENABLE_LOGGING
        m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
        m_iRcvLastSkipAck = m_iRcvLastAck;
        m_iRcvLastAckAck = isn;
        m_iRcvCurrSeqNo = CSeqNo::decseq(isn);
    }

private: // Timers
    time_point m_tsLastAckTime;                  // Timestamp of last ACK

private: // Intervals
    duration m_tdACKInterval;                    // ACK interval
    duration m_tdNAKInterval;                    // NAK interval
    duration m_tdMinNakInterval;                 // NAK timeout lower bound; too small value can cause unnecessary retransmission

private:
    SRT_ATTR_GUARDED_BY(m_RcvLossLock)
    CRcvLossList* m_pRcvLossList;                //< Receiver loss list
    std::deque<CRcvFreshLoss> m_FreshLoss;       //< Lost sequence already added to m_pRcvLossList, but not yet sent UMSG_LOSSREPORT for.
    int m_iReorderTolerance;                     //< Current value of dynamic reorder tolerance
    int m_iConsecEarlyDelivery;                  //< Increases with every OOO packet that came <TTL-2 time, resets with every increased reorder tolerance
    int m_iConsecOrderedDelivery;                //< Increases with every packet coming in order or retransmitted, resets with every out-of-order packet

    CACKWindow<ACK_WND_SIZE> m_ACKWindow;        // ACK history window
    CPktTimeWindow<16, 64> m_RcvTimeWindow;      // Packet arrival time window

    int32_t m_iRcvLastAck;                       // First unacknowledged packet seqno sent in the latest ACK.
#ifdef ENABLE_LOGGING
    int32_t m_iDebugPrevLastAck;
#endif
    int32_t m_iRcvLastSkipAck;                   // Last dropped sequence ACK
    int32_t m_iRcvLastAckAck;                    // Last sent ACK that has been acknowledged
    int32_t m_iAckSeqNo;                         // Last ACK sequence number
    sync::atomic<int32_t> m_iRcvCurrSeqNo;       // Largest received sequence number
    int32_t m_iRcvCurrPhySeqNo;                  // Same as m_iRcvCurrSeqNo, but physical only (disregarding a filter)
}

} // namespace srt

