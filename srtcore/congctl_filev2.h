//#include "ext_hash_map.h"
#include <cmath>
#include <map>
#include <vector>
#include <algorithm>

#include "core.h"
#include "congctl.h"
#include "utilities.h"
#include "logging.h"


using namespace std;
using namespace srt_logging;

// Useful macro to shorthand passing a method as argument
// Requires "Me" name by which a class refers to itself
#define SSLOT(method) EventSlot(this, &Me:: method)


class FileV2CC : public SrtCongestionControlBase
{
    typedef FileV2CC Me; // Required by SSLOT macro

    // Fields from CCC not used by LiveSmoother
    int m_iACKPeriod;

    // Fields from CUDTCC
    int m_iRCInterval;			// UDT Rate control interval
    uint64_t m_LastRCTime;		// last rate increase time
    bool m_bSlowStart;			// if in slow start phase
    int32_t m_iLastAck;			// last ACKed seq no
    bool m_bLoss;			// if loss happened since last rate increase
    int32_t m_iLastDecSeq;		// max pkt seq no sent out when last decrease happened
    double m_dLastDecPeriod;		// value of pktsndperiod when last decrease happened
    int m_iNAKCount;                     // NAK counter
    int m_iDecRandom;                    // random threshold on decrease by number of loss events
    int m_iAvgNAKNum;                    // average number of NAKs per congestion
    int m_iDecCount;			// number of decreases in a congestion epoch

    int64_t m_maxSR;

public:
    FileV2CC(CUDT* parent) : SrtCongestionControlBase(parent)
    {
        // Note that this function is called at the moment of
        // calling m_Smoother.configure(this). It is placed more less
        // at the same position as the series-of-parameter-setting-then-init
        // in the original UDT code. So, old CUDTCC::init() can be moved
        // to constructor.

        m_iRCInterval = CUDT::COMM_SYN_INTERVAL_US;
        m_LastRCTime = CTimer::getTime();
        m_iACKPeriod = CUDT::COMM_SYN_INTERVAL_US;

        m_bSlowStart = true;
        m_iLastAck = parent->sndSeqNo();
        m_bLoss = false;
        m_iLastDecSeq = CSeqNo::decseq(m_iLastAck);
        m_dLastDecPeriod = 1;
        m_iAvgNAKNum = 0;
        m_iDecCount = 0;
        m_iNAKCount = 0;
        m_iDecRandom = 1;

        // SmotherBase
        m_dCWndSize = 16;
        m_dPktSndPeriod = 1;

        m_maxSR = 0;

        parent->ConnectSignal(TEV_ACK, SSLOT(updateSndPeriod));
        parent->ConnectSignal(TEV_LOSSREPORT, SSLOT(slowdownSndPeriod));
        parent->ConnectSignal(TEV_CHECKTIMER, SSLOT(speedupToWindowSize));

        HLOGC(mglog.Debug, log << "Creating FileSmoother V2");
    }

    bool checkTransArgs(SrtCongestion::TransAPI, SrtCongestion::TransDir, const char*, size_t, int, bool) ATR_OVERRIDE
    {
        // XXX
        // The FileSmoother has currently no restrictions, although it should be
        // rather required that the "message" mode or "buffer" mode be used on both sides the same.
        // This must be somehow checked separately.
        return true;
    }

    bool needsQuickACK(const CPacket & pkt) ATR_OVERRIDE
    {
        // For FileSmoother, treat non-full-buffer situation as an end-of-message situation;
        // request ACK to be sent immediately.
        if (pkt.getLength() < m_parent->maxPayloadSize())
            return true;

        return false;
    }

    void updateBandwidth(int64_t maxbw, int64_t) ATR_OVERRIDE
    {
        if (maxbw != 0)
        {
            m_maxSR = maxbw;
            HLOGC(mglog.Debug, log << "FileV2CC: updated BW: " << m_maxSR);
        }
    }

private:

    // SLOTS
    void updateSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        const int ack = arg.get<EventVariant::ACK>();

        double inc = 0;

        uint64_t currtime = CTimer::getTime();
        if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
            return;

        m_LastRCTime = currtime;

        if (m_bSlowStart)
        {
            m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);
            m_iLastAck = ack;

            if (m_dCWndSize > m_dMaxCWndSize)
            {
                m_bSlowStart = false;
                if (m_parent->deliveryRate() > 0)
                {
                    m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                    LOGC(mglog.Debug, log << "FileV2CC: UPD (slowstart:ENDED) wndsize="
                        << m_dCWndSize << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod << "us = 1M/("
                        << m_parent->deliveryRate() << " pkts/s)");
                }
                else
                {
                    m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                    LOGC(mglog.Debug, log << "FileV2CC: UPD (slowstart:ENDED) wndsize="
                        << m_dCWndSize << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod << "us = wndsize/(RTT+RCIV) RTT="
                        << m_parent->RTT() << " RCIV=" << m_iRCInterval);
                }
            }
            else
            {
                LOGC(mglog.Debug, log << "FileV2CC: UPD (slowstart:KEPT) wndsize="
                    << m_dCWndSize << "/" << m_dMaxCWndSize
                    << " sndperiod=" << m_dPktSndPeriod << "us");
            }
        }
        else
        {
            m_dCWndSize = m_parent->deliveryRate() / 1000000.0 * (m_parent->RTT() + m_iRCInterval) + 16;
            LOGC(mglog.Debug, log << "FileV2CC: UPD (speed mode) wndsize="
                << m_dCWndSize << "/" << m_dMaxCWndSize << " RTT = " << m_parent->RTT()
                << " sndperiod=" << m_dPktSndPeriod << "us. deliverRate = "
                << m_parent->deliveryRate() << " pkts/s)");
        }


        if (!m_bSlowStart)
        {
            if (m_bLoss)
            {
                m_bLoss = false;
            }
            // During Slow Start, no rate increase
            else
            {
                const int loss_bw = 2 * (1000000 / m_dLastDecPeriod); // 2 times last loss point
                const int bw_pktps = min(loss_bw, m_parent->bandwidth());

                int64_t B = (int64_t)(bw_pktps - 1000000.0 / m_dPktSndPeriod);
                if ((m_dPktSndPeriod > m_dLastDecPeriod) && ((bw_pktps / 9) < B))
                    B = bw_pktps / 9;
                if (B <= 0)
                    inc = 1.0 / m_parent->MSS();    // was inc = 0.01;
                else
                {
                    // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
                    // Beta = 1.5 * 10^(-6)

                    inc = pow(10.0, ceil(log10(B * m_parent->MSS() * 8.0))) * 0.0000015 / m_parent->MSS();

                    if (inc < 1.0 / m_parent->MSS())  // was < 0.01 then 0.01
                        inc = 1.0 / m_parent->MSS();
                }

                LOGC(mglog.Debug, log << "FileV2CC: UPD (slowstart:OFF) loss_bw=" << loss_bw
                    << " bandwidth=" << m_parent->bandwidth() << " inc=" << inc
                    << " m_dPktSndPeriod=" << m_dPktSndPeriod
                    << "->" << (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval));

                m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);
            }
        }

#if 1 //ENABLE_HEAVY_LOGGING
        // Try to do reverse-calculation for m_dPktSndPeriod, as per minSP below
        // sndperiod = mega / (maxbw / MSS)
        // 1/sndperiod = (maxbw/MSS) / mega
        // mega/sndperiod = maxbw/MSS
        // maxbw = (MSS*mega)/sndperiod
        uint64_t usedbw = (m_parent->MSS() * 1000000.0) / m_dPktSndPeriod;

#if defined(unix) && defined (SRT_ENABLE_SYSTEMBUFFER_TRACE)
        // Check the outgoing system queue level
        int udp_buffer_size = m_parent->sndQueue()->sockoptQuery(SOL_SOCKET, SO_SNDBUF);
        int udp_buffer_level = m_parent->sndQueue()->ioctlQuery(TIOCOUTQ);
        int udp_buffer_free = udp_buffer_size - udp_buffer_level;
#else
        int udp_buffer_free = -1;
#endif

        LOGC(mglog.Debug, log << "FileV2CC: UPD (slowstart:"
            << (m_bSlowStart ? "ON" : "OFF") << ") wndsize=" << m_dCWndSize << " inc = " << inc
            << " sndperiod=" << m_dPktSndPeriod << "us BANDWIDTH USED:" << usedbw << " (limit: " << m_maxSR << ")"
            " SYSTEM BUFFER LEFT: " << udp_buffer_free);
#endif

        //set maximum transfer rate
        if (m_maxSR)
        {
            double minSP = 1000000.0 / (double(m_maxSR) / m_parent->MSS());
            if (m_dPktSndPeriod < minSP)
            {
                m_dPktSndPeriod = minSP;
                LOGC(mglog.Debug, log << "FileV2CC: BW limited to " << m_maxSR
                    << " - SLOWDOWN sndperiod=" << m_dPktSndPeriod << "us");
            }
        }

    }

    // When a lossreport has been received, it might be due to having
    // reached the available bandwidth limit. Slowdown to avoid further losses.
    void slowdownSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        const int32_t* losslist = arg.get_ptr();
        size_t losslist_size = arg.get_len();

        // Sanity check. Should be impossible that TEV_LOSSREPORT event
        // is called with a nonempty loss list.
        if (losslist_size == 0)
        {
            LOGC(mglog.Error, log << "IPE: FileV2CC: empty loss list!");
            return;
        }

        //Slow Start stopped, if it hasn't yet
        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
            {
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                LOGC(mglog.Debug, log << "FileV2CC: LOSS, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS mega/rate (rate="
                    << m_parent->deliveryRate() << ")");
            }
            else
            {
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                LOGC(mglog.Debug, log << "FileV2CC: LOSS, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS wndsize/(RTT+RCIV) (RTT="
                    << m_parent->RTT() << " RCIV=" << m_iRCInterval << ")");
            }

        }

        m_bLoss = true;

        const int pktsInFlight = m_parent->RTT() / m_dPktSndPeriod;
        const int ack_seqno = m_iLastAck;// m_parent->sndLastDataAck();
        const int sent_seqno = m_parent->sndSeqNo();
        const int num_pkts_sent = CSeqNo::seqlen(ack_seqno, sent_seqno);
        //const int num_pkts_reached = num_pkts_sent > pktsInFlight ? (num_pkts_sent - pktsInFlight) : num_pkts_sent;
        const int num_pkts_lost = m_parent->sndLossLength();
        const int lost_pcent_x10 = pktsInFlight > 0 ? (num_pkts_lost * 1000) / pktsInFlight : 0;

        LOGC(mglog.Debug, log << "FileSmootherV2: LOSS: "
            << "sent=" << num_pkts_sent << ", inFlight=" << pktsInFlight
            << ", lost=" << num_pkts_lost << " ("
            << lost_pcent_x10 / 10 << "." << lost_pcent_x10 % 10 << "\%)");
        if (lost_pcent_x10 < 20)    // 2.0%
        {
            LOGC(mglog.Debug, log << "FileSmootherV2: LOSS: m_dLastDecPeriod=" << m_dLastDecPeriod << "->" << m_dPktSndPeriod);
            m_dLastDecPeriod = m_dPktSndPeriod;
            return;
        }

        // In contradiction to UDT, TEV_LOSSREPORT will be reported also when
        // the lossreport is being sent again, periodically, as a result of
        // NAKREPORT feature. You should make sure that NAKREPORT is off when
        // using FileV2CC, so relying on SRTO_TRANSTYPE rather than
        // just SRTO_SMOOTHER is recommended.
        int32_t lossbegin = SEQNO_VALUE::unwrap(losslist[0]);

        if (CSeqNo::seqcmp(lossbegin, m_iLastDecSeq) > 0)
        {
            m_dLastDecPeriod = m_dPktSndPeriod;
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.03);

            m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.97 + m_iNAKCount * 0.03);
            m_iNAKCount = 1;
            m_iDecCount = 1;

            m_iLastDecSeq = m_parent->sndSeqNo();

            // remove global synchronization using randomization
            srand(m_iLastDecSeq);
            m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
            if (m_iDecRandom < 1)
                m_iDecRandom = 1;
            LOGC(mglog.Debug, log << "FileV2CC: LOSS:NEW lseqno=" << lossbegin
                << ", lastsentseqno=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", rand=" << m_iDecRandom
                << " avg NAK:" << m_iAvgNAKNum
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
        else if ((m_iDecCount++ < 5) && (0 == (++m_iNAKCount % m_iDecRandom)))
        {
            // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.03);
            m_iLastDecSeq = m_parent->sndSeqNo();
            LOGC(mglog.Debug, log << "FileV2CC: LOSS:PERIOD lseqno=" << lossbegin
                << ", lastsentseqno=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", deccnt=" << m_iDecCount
                << ", decrnd=" << m_iDecRandom
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
        else
        {
            LOGC(mglog.Debug, log << "FileV2CC: LOSS:STILL lseqno=" << lossbegin
                << ", lastsentseqno=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", deccnt=" << m_iDecCount
                << ", decrnd=" << m_iDecRandom
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }

    }

    void speedupToWindowSize(ETransmissionEvent, EventVariant arg)
    {
        ECheckTimerStage stg = arg.get<EventVariant::STAGE>();

        // TEV_INIT is in the beginning of checkTimers(), used
        // only to synchronize back the values (which is done in updateCC
        // after emitting the signal).
        if (stg == TEV_CHT_INIT)
            return;

        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
            {
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                LOGC(mglog.Debug, log << "FileV2CC: CHKTIMER, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS mega/rate (rate="
                    << m_parent->deliveryRate() << ")");
            }
            else
            {
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                LOGC(mglog.Debug, log << "FileV2CC: CHKTIMER, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS wndsize/(RTT+RCIV) (wndsize="
                    << setprecision(6) << m_dCWndSize << " RTT=" << m_parent->RTT() << " RCIV=" << m_iRCInterval << ")");
            }
        }
        else
        {
            // XXX This code is a copy of legacy CUDTCC::onTimeout() body.
            // This part was commented out there already.
            /*
               m_dLastDecPeriod = m_dPktSndPeriod;
               m_dPktSndPeriod = ceil(m_dPktSndPeriod * 2);
               m_iLastDecSeq = m_iLastAck;
             */
        }
    }

    SrtCongestion::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return SrtCongestion::SRM_LATEREXMIT;
    }
};
#undef SSLOT

