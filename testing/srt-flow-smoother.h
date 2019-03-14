#include "smoother.h"
#include "core.h"
#include "utilities.h"

//#include "ext_hash_map.h"
#include <cmath>
#include <map>
#include <vector>

using namespace std;

class FlowSmoother: public SmootherBase
{
    // Fields from CUDTCC
    const int m_iRCInterval;			// UDT Rate control interval
    uint64_t m_LastRCTime;		// last rate increase time
    int32_t m_iLastRCTimeAck;			// last ACKed seq no
    int32_t m_iLastRCTimeSndSeq;		// max pkt seq no sent out when last decrease happened
    double m_dLastDecPeriod;		// value of pktsndperiod when last decrease happened
    int m_iNAKCount;                     // NAK counter
    int m_iDecRandom;                    // random threshold on decrease by number of loss events
    int m_iAvgNAKNum;                    // average number of NAKs per congestion
    int m_iDecCount;			// number of decreases in a congestion epoch

    uint64_t m_LastAckTime;     // last time when ACK was received
    int32_t m_LastAckSeq;      // ACK sequence number recorded last time

    typedef FlowSmoother Me; // required for SSLOT macro

    // Loss tracking.
    //
    // When a loss is spotted, it usually means that the packets
    // are being sent too fast. However we can't say what exactly
    // happened without getting more information about the loss.
    // Things that need to be done are:
    //
    // 1. Exit the FS_WARMUP state immediately - sending with maximum
    //    possible speed might choke the transmission completely.
    // 2. Start doing the short loss measurement (LossSpeed), that is
    //    initialize the index and collect losses in consecutive short cells.
    //    When the FS_SAMPLE_WIDTH is reached earlier (which means that
    //    packet loss is below 25%) it means that the loss is not tragic,
    //    can be measured with the normal measurement process.
    // 3. Do the 4-times loss measurement:
    //    - with every loss, check if the number of lost packets exceed 16.
    //      if so, its treated as a choke point and decreasing the speed must
    //      happen immediately
    //    - Fill the loss information for every range of 16 packets, by
    //      checking the sequence numbers of the loss.
    //    - When filled the whole 4 cells, check the percentage of packet loss.
    //      if the percentage of the packet loss increases, switch to FS_SLIDE.
    // 4. When the loss has no increasing tendency, switch to FS_CLIMB and start
    //    long term measurements.

    struct SeqRange
    {
        int32_t begin, end;
        size_t size;
        SeqRange(): begin(), end(), size() {}

        void set_including(int32_t from, int32_t to)
        {
            begin = from;
            end = to;
            // seqlen's precondition is that the 1st sequence is earlier than the 2nd.
            size = CSeqNo::seqlen(begin, end);
        }

        void set_excluding(int32_t from, int32_t to)
        {
            if (from == to)
                return; // ignore empty ranges
            begin = from;
            end = CSeqNo::decseq(to);
            size = CSeqNo::seqlen(begin, end);
        }

        void set_one(int32_t val)
        {
            begin = val;
            end = val;
            size = 1;
        }
    };

    // This is the sequence number that is an "accidental acknowledgement"
    // which is the highest number of lost sequence + 2 (+1 would be the exact
    // sequence of the packet received, one more +1 makes it an ACK sequence).
    // This is a packet that has been confirmed reception by the fact that it
    // immediately follows the lost one.
    int32_t m_LastLossAckSeq;

    // These fields collect the lost and acknowledged
    // sequence ranges reported at the LOSSREPORT event.
    // They collect the extra packet loss rate information
    // that is unavailable yet for this range of sent packets
    // (because it comes only with ACK message).
    vector<SeqRange> m_UnackLossRange;

    double m_dLastPktSndPeriod;
    double m_dTimedLossRate;         // Timed loss rate (packets lost per microsecond)
    double m_dLossRate;              // Percentage loss rate (packets lost in the number of packets transmitted)
    uint64_t m_SenderRTT_us;   // Sender RTT (time between sending data packet and receiving ACK for it)

    int m_SenderSpeed_pps;
    int m_MaxSenderSpeed_pps;
    int64_t m_SenderSpeed_bps;
    int m_MaxReceiverSpeed_pps; // Value for deliveryRate, but direct and noted as maximum.
    int64_t m_LastGoodSndPeriod_us; // Last high speed that was used before slowdown by packet loss happened

    JumpingAverage<int, 3, 4> m_SenderSpeedMeasure;
    JumpingAverage<int, 3, 4> m_SenderRTTMeasure;
    JumpingAverage<double, 3, 2> m_LossRateMeasure;

    int m_LastRcvVelocity;
    int m_LastRcvSpeed;
    int m_LastSenderRTT;

    bool m_bLoss; // XXX temporarily kept for the time of experiments

    size_t m_zTimerCounter;

    RingLossyStack<int, 4> m_LossRateHistory;

    // XXX Congestion Window measurement is probably useless.
    // Even though this means something, it undergoes quite long
    // lasting transient state, so any decision based on it may be too late.
    double m_dLastCWNDSize;
    RingLossyStack<double, 16> m_CWNDMeasure;
    int m_iCWNDMeasureStage;

    // The long time measurements rely on collecting information with every
    // received UMSG_ACK and handled in the TEV_ACK handler.
    //
    // This should take the short-term values of:
    // - delivery speed: 1/sender_RTT, where sender_RTT is time distance bw. sending packet SEQ=X, where ACK_SEQ=X+1.
    // - receiving speed: the momentary (NOT average) receiving speed as calculated by receiver (ACKD_RCVVELOCITY)
    // - sending speed: locally measured through time distance between sending consecutive packets
    // - loss rate: how many percent of the packets were lost in this ack period
    // - congestion rate: how the flight size has changed towards the previous period

    struct Probe
    {
        double snd_period;
        int64_t tx_velocity, rx_velocity, rx_speed;
        uint64_t sender_rtt;
        int ack_size, uack_size;
        double lossrate;
        double congestion_rate;

        void update(size_t index, const Probe& nw)
        {
            // By zero value of snd_period, you can recognize
            // that this probe wasn't filled yet.
            if (index == 0 || snd_period == 0)
            {
                *this = nw;
                return;
            }

#define AV(field) field = avg_iir(index+1, field, nw.field)
            AV(snd_period);
            AV(tx_velocity);
            AV(rx_velocity);
            AV(rx_speed);
            AV(lossrate);
            AV(congestion_rate);
#undef AV
        }

        template <class ValueType>
        struct Field
        {
            ValueType Probe::*fptr;

            Field(ValueType Probe::*v): fptr(v) {}

            ValueType operator()(const Probe& item)
            {
                return item.*fptr;
            }
        };

        template <class ValueType>
        static Field<ValueType> field(ValueType Probe::*fptr)
        { return Field<ValueType>(fptr); }

        template <class ValueType>
        struct FieldLess
        {
            ValueType Probe::*fptr;

            FieldLess(ValueType Probe::*v): fptr(v) {}

            bool operator()(const Probe& earlier, const Probe& later)
            {
                return earlier.*fptr < later.*fptr;
            }
        };

        template <class ValueType>
        static FieldLess<ValueType> field_less(ValueType Probe::*fptr)
        { return FieldLess<ValueType>(fptr); }
    };

    // 16 consecutive ACK events are used to make measurements, with
    // jumping averate with segment size of 4. Decisions are made depending
    // on the control state:

    // FS_WARMUP.
    //
    // No decision is undertaken until the first loss is found or until
    // the flight size reaches the size of the congestion window. If a clean
    // exit was done from the FS_WARMUP state, it switches to FS_SLIDE.
    // When a loss happened during this period, this causes immediate switch
    // to FS_CLIMB.
    //
    // FS_CLIMB.
    //
    // This happens when the WARMUP was interrupted abnormally with the prediction
    // that further speeding up may choke the link completely. In this state the
    // speed in increased with every 2nd ACK, and the results are constantly measured.
    // In the beginning there is no top speed found, so the speedup is finished if
    // the consecutive 16 speed increases caused consequent decreasing of the receiver
    // speed, delivery speed (decreased RTT), or packet loss.
    //
    // FS_SLIDE.
    //
    // The speed is first set to the level of the received value of ACKD_RCVSPEED.
    // Then the speed is being slowed down with expectation to find the maximum value
    // of the reciving speed as measured through ACKD_RCVVELOCITY and delivery speed
    // (1/RTT). If no maximum found during this measurement, return to the previous
    // speed and switch to FS_CLIMB (as this means that no maximum speed has been measured).
    // Otherwise switch to FS_KEEP.
    //
    // FS_KEEP.
    //
    // In this state there should be the already calculated optimal speed used,
    // and it's already known the Pv speed range. The next 4 consecutive
    // measurements should state as to whether the speed has decreased to the
    // level of the lowest of the speeds at the Pv borders, in such a situation the
    // measurements start anew. If the maximum was found exactly at the highest
    // speed, set this speed as current and switch to FS_CLIMB. If at the lowest
    // speed - switch to FS_SLIDE.

    size_t m_zProbeSize;
    size_t m_zProbeSpan;
    // This is for collecting results from consecutive S times (S = m_zProbeSize).
    // Values here will be updated with IIR-calculated average, then reset. When
    // resetting the calculated value will be placed in m_adStats.

    static const size_t FS_SAMPLE_WIDTH = 32;
    static const size_t FS_HISTORY_SIZE = 4;

    // After collecting consecutive results in m_adProbe, collect them with snapping
    // to grid into this array.
    Probe m_adStats[FS_SAMPLE_WIDTH];

    enum FlowState { FS_WARMUP, FS_COOLDOWN, FS_SLIDE, FS_CLIMB, FS_KEEP, FS_BITE, FlowState__size };

    struct State
    {
        FlowState state;
        unsigned probe_index;
        unsigned span_index;

        State(FlowState s): state(s), probe_index(), span_index() {}
    } m_State;

    FlowState m_PrevState;

    struct ProbeHistoryEntry
    {
        Probe probe; // Final "best" values.
        FlowState state; // State that was there previously
        std::pair<double, double> speedRange;
        double stepfactor;
    };

    RingLossyStack<ProbeHistoryEntry, FS_HISTORY_SIZE> m_ProbeHistory;

    std::string DisplayState(const State& st)
    {
        static std::string stnames[int(FlowState__size)] = {"WARMUP", "COOLDOWN", "SLIDE", "CLIMB", "KEEP", "BITE"};
        std::ostringstream os;
        os << stnames[st.state] << "[" << setfill('_') << setw(2) << st.probe_index << "." << st.span_index << "]";
        return os.str();
    }

    int64_t m_llMaxBW;

    double m_dPktSndPeriod_SLOWEST_us;
    double m_dPktSndPeriod_OPTIMAL_us;
    double m_dPktSndPeriod_FASTEST_us;

    static constexpr double MAX_SPEED_SND_PERIOD = 1;      // 1ms bw packets
    static constexpr double MIN_SPEED_SND_PERIOD = 250000; // 0.25s bw packets

public:
    FlowSmoother(CUDT* parent):
        SmootherBase(parent),
        m_iRCInterval(CUDT::COMM_SYN_INTERVAL_US), // == 10ms
        m_State(FS_WARMUP)
    {
        // Note that this function is called at the moment of
        // calling m_Smoother.configure(this). It is placed more less
        // at the same position as the series-of-parameter-setting-then-init
        // in the original UDT code. So, old CUDTCC::init() can be moved
        // to constructor.

        m_LastRCTime = CTimer::getTime();

        m_iLastRCTimeAck = m_parent->sndSeqNo();
        m_iLastRCTimeSndSeq = CSeqNo::decseq(m_iLastRCTimeAck);
        m_dLastDecPeriod = 1;
        m_iAvgNAKNum = 0;
        m_iNAKCount = 0;
        m_iDecRandom = 1;
        m_dLastPktSndPeriod = 1;
        m_dLossRate = 0;
        m_dTimedLossRate = 0;
        m_bLoss = false;
        m_SenderRTT_us = 0;
        m_SenderSpeed_pps = 0;
        m_MaxSenderSpeed_pps = 0;
        m_SenderSpeed_bps = 0;
        m_MaxReceiverSpeed_pps = 0;
        m_LastRcvVelocity = 0;
        m_LastRcvSpeed = 0;
        m_LastGoodSndPeriod_us = 0;
        m_LastSenderRTT = 0;
        m_zTimerCounter = 0;

        m_LastAckTime = 0; // Don't measure anything at the first ever ACK
        m_LastAckSeq = m_LastLossAckSeq = m_iLastRCTimeAck;

        memset(&m_adStats, 0, sizeof(m_adStats));

        m_zProbeSize = FS_SAMPLE_WIDTH; // Initial for FS_WARMUP, might be changed later.
        m_zProbeSpan = 1;

        // SmotherBase
        m_dCWndSize = 16;
        m_dPktSndPeriod = 1;

        m_dLastCWNDSize = 16;
        m_iCWNDMeasureStage = 0;

        // Initial zero to declare them "not measured yet"
        m_dPktSndPeriod_FASTEST_us = 0;
        m_dPktSndPeriod_SLOWEST_us = 0;
        m_dPktSndPeriod_OPTIMAL_us = 0;

        m_llMaxBW = 0;

        parent->ConnectSignal(TEV_ACK, EventSlot(this, &Me::onACK));
        parent->ConnectSignal(TEV_LOSSREPORT, EventSlot(this, &Me::onLOSS_CollectLoss));
        parent->ConnectSignal(TEV_CHECKTIMER, EventSlot(this, &Me::onTIMER));

        m_PrevState = FS_WARMUP;

        HLOGC(mglog.Debug, log << "Creating FlowSmoother");
    }

    bool checkTransArgs(Smoother::TransAPI, Smoother::TransDir, const char* , size_t , int , bool ) ATR_OVERRIDE
    {
        // XXX
        // The FlowSmoother has currently no restrictions, although it should be
        // rather required that the "message" mode or "buffer" mode be used on both sides the same.
        // This must be somehow checked separately.
        return true;
    }

    bool needsQuickACK(const CPacket& pkt) ATR_OVERRIDE
    {
        // For FlowSmoother, treat non-full-buffer situation as an end-of-message situation;
        // request ACK to be sent immediately.
        if (pkt.getLength() < m_parent->maxPayloadSize())
            return true;

        return false;
    }

    void updateBandwidth(int64_t maxbw, int64_t) ATR_OVERRIDE
    {
        if (maxbw != 0)
        {
            m_llMaxBW = maxbw;
            HLOGC(mglog.Debug, log << "FlowSmoother: updated BW: " << m_llMaxBW);
        }
    }

private:

    void updateCongestionWindowSize(int32_t ack)
    {
        if (m_State.state == FS_WARMUP)
        {
            // During Slow Start period, update with the number of
            // newly ack-ed packets, and if exceeds the maximum, exit Slow Start.
            // Otherwise update the size from delivery rate and RTT.
            m_dCWndSize += CSeqNo::seqlen(m_iLastRCTimeAck, ack);
            m_iLastRCTimeAck = ack;

            if (m_dCongestionWindow > m_dMaxCWndSize)
            {
                setCooldown(false, "UPD/ACK");
            }
            else
            {
                HLOGC(mglog.Debug, log << "FlowSmoother: UPD/ACK (slowstart:KEPT) wndsize="
                    << m_dCongestionWindow << "/" << m_dMaxCWndSize
                    << " sndperiod=" << m_dPktSndPeriod << "us");
            }
        }
        else
        {
            m_dCWndSize = m_parent->deliveryRate() / 1000000.0 * (m_parent->RTT() + m_iRCInterval) + 16;
        }
    }

    void setCooldown(bool spike, const char* hdr)
    {
        // If we have reached the top window, set the speed
        // to the receiver speed, then keep it for the next 16 probes.
        // Set the current speed to keep for the next 16 ACKs
        // then try to speed up.

        int exp_speed = m_parent->deliveryRate();
        double snd_period;

        if (spike)
        {
            // We must slow down because the network is already choking, so the
            // sender can't even continue with the optimum speed. The sending
            // speed must be cut rapidly for the next 8 probes.
            int best_speed = std::max(exp_speed, m_MaxReceiverSpeed_pps);
            double snd_period = 1000000.0/best_speed;

            // Make it run 10* slower than the maximum reached speed.
            // The cooldown state can be interrupted at any time if the loss
            // rate drops to 0.
            m_dPktSndPeriod = snd_period * 10;
            HLOGC(mglog.Debug, log << "FlowSmoother: " << hdr << " (exit WARMUP/spike) CWND="
                    << setprecision(6) << m_dCongestionWindow << "/" << m_dMaxCWndSize
                    << " sndperiod=" << m_dPktSndPeriod << "us [1/10 of HIGHEST SPEED OBSERVED: "
                    << best_speed << "p/s]");

            switchState(FS_COOLDOWN, 8, 1, m_dPktSndPeriod, best_speed);

        }
        else
        {
            if (m_State.state == FS_COOLDOWN)
            {
                // Cooldown was re-requested, it means that even though we slowed down,
                // seems like it's not enough. Make the sender period twice more.
                snd_period = m_dPktSndPeriod  * 2;
            }
            else if (exp_speed > 0)
            {
                snd_period = 1000000.0/exp_speed;
                HLOGC(mglog.Debug, log << "FlowSmoother: " << hdr << " (reached TOP) CWND="
                        << setprecision(6) << m_dCongestionWindow << "/" << m_dMaxCWndSize
                        << " sndperiod=" << snd_period << "us [EQUAL TO DELIVERY RATE: "
                        << m_parent->deliveryRate() << "p/s]");
            }
            else
            {
                snd_period = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                HLOGC(mglog.Debug, log << "FlowSmoother: " << hdr << " (reached TOP) CWND="
                        << setprecision(6) << snd_period << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod
                        << "us [BASED ON RTT+RCI=" << ((m_parent->RTT() + m_iRCInterval)/1000.0)
                        << "ms = " << (1000000.0/(m_parent->RTT() + m_iRCInterval)) << "Hz of CWND]");
            }

            m_dPktSndPeriod = snd_period;
            double best_speed = std::max(exp_speed, m_MaxReceiverSpeed_pps);
            double speed_diff = m_MaxReceiverSpeed_pps - exp_speed;
            if (speed_diff > 0)
                best_speed += speed_diff/4;

            double fastest_period = 1000000.0/best_speed;

            switchState(FS_COOLDOWN, 8, 1, m_dPktSndPeriod, fastest_period);
        }

        // Set the current speed as slowest and clear up the top speed.
        // It is tempting to use m_MaxReceiverSpeed_pps as a base for the maximum
        // speed, but the problem is that with a spike it's no longer reliable.
        // The exit of cooldown can use more measurement to make a better decision.
    }

    void switchState(FlowState state, size_t probesize, size_t probespan,
            uint64_t slowest, uint64_t fastest)
    {
        m_State = State(state);
        m_zProbeSize = probesize;
        m_zProbeSpan = probespan;

        m_dPktSndPeriod_SLOWEST_us = slowest;
        m_dPktSndPeriod_FASTEST_us = fastest;
        HLOGC(mglog.Debug, log << "FlowSmoother: SWITCH STATE: " << DisplayState(m_State) << "PROBE:["
            << m_zProbeSize << "." << m_zProbeSpan << "] sndperiod=<" << slowest << " .. " << fastest << ">" );
    }

    void emergencyBrake()
    {
        // This should be done when the loss has exceeded 50%.
        // What we do is to set the sender period to twice that value
        // and switch to keep to observe the results. This might end
        // up with another emergency break until the rate drops below 50%.
        // When reached the minimum speed, do not do any further slowdown,
        // no matter how big the loss rate will result. Worst case scenario,
        // if we have a 50% packet loss with 0.25s distance in time, we'll get
        // connection timeout very soon.

        double slower = 2 * m_dPktSndPeriod;
        if (slower > MIN_SPEED_SND_PERIOD)
            slower = MIN_SPEED_SND_PERIOD;

        HLOGC(mglog.Debug, log << "FlowSmoother: LOSS >50%, EMERGENCY BRAKE. sndperiod=" << slower);

        // Set the lowest speed as the current speed and highest as 0 (to be measured).
        //switchState(FS_KEEP, 16, 1, slower, 0);
        switchState(FS_COOLDOWN, 8, 1, slower, 0);
        m_dPktSndPeriod = slower;
    }

    void measureSenderSpeed()
    {
        int pps;
        int64_t bps;
        bool got = m_parent->updateSenderSpeed(Ref(pps), Ref(bps));
        if (!got)
            return;

        // First time measured
        if (m_MaxSenderSpeed_pps == 0)
        {
            m_MaxSenderSpeed_pps = pps;
            m_SenderSpeed_pps = pps;
            m_SenderSpeed_bps = bps;
        }
        else
        {
            m_SenderSpeed_bps = avg_iir<4>(m_SenderSpeed_bps, bps);
            m_SenderSpeed_pps = avg_iir<4>(m_SenderSpeed_pps, pps);
        }

    }

    // SLOTS
    void onACK(ETransmissionEvent, TevAckData ad)
    {
        // Normally the speed analysis and state switch decision
        // should be made when the probe index has reached the probe size.
        // But there can be also special conditions, when the state should
        // be changed earlier, before the probe is full.
        bool switch_time = false;

        m_zTimerCounter = 0; // Clear the timer that counts TIMERS between ACKs

        uint64_t currtime = CTimer::getTime();

        uint64_t ack_period = currtime - m_LastAckTime;
        if (ack_period == 0)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: IPE: impossible same-time ACK as previous @ " << logging::FormatTime(m_LastAckTime));
            // Some mistake, would make it div/0. Ignore the event.
            return;
        }

        std::string decision = "[UPD]";

        int32_t farthest_ack = CSeqNo::incseq(m_parent->sndSeqNo());
        int flight_span = CSeqNo::seqlen(ad.ack, farthest_ack) - 1;

        // THIS is the only thing done in the update
        bool sp_updated = updateSndPeriod(currtime, ad.ack, Ref(decision));
        if (sp_updated && m_State.state == FS_BITE)
        {
            // Remember the lowest and highest speed while in BITE state
            // (in other states these values shall not be changed because they
            // are used to embrace the measurement range).
            if (m_dPktSndPeriod_FASTEST_us > m_dPktSndPeriod || m_dPktSndPeriod_FASTEST_us == 0)
                m_dPktSndPeriod_FASTEST_us = m_dPktSndPeriod;
        }

        int32_t* ackdata;
        int acksize;
        Tie2(ackdata, acksize) = m_parent->rcvAckDataCache();

        if (acksize == 1)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: LITE ACK DETECTED, not doing anything");
            // For LITE ACK don't make any measurements.
            // (you don't have appropriate data to do it).
            return;
        }

        int32_t last_ack = m_LastAckSeq;
        m_LastAckSeq = ad.ack;

        // if ad.ack %> m_LastLossAckSeq
        if (CSeqNo::seqcmp(ad.ack, m_LastLossAckSeq) > 0)
        {
            // update the loss-based ACK so that it's never earlier than ACK.
            // Don't update it when m_LastLossAckSeq is still greater than ad.ack
            // because this means that the packets reported as lost before the
            // current ACK were not yet recovered. For the loss calculation it's
            // better to include all packets that were sent except those still
            // in flight. It's especially important for getting more accurate
            // loss results in case when the loss grow high fast and the algo
            // needs to react quickly.
            m_LastLossAckSeq = ad.ack;
        }
        int32_t last_rcv = acksize > ACKD_RCVLASTSEQ ? ackdata[ACKD_RCVLASTSEQ] : 0;

        // XXX Not used currently - might be used here to
        // calculate the "ack speed", but there's no use of 
        // this data so far.
        // uint64_t last_time = m_LastAckTime;
        m_LastAckTime = currtime;

        double last_cwnd_size = m_dLastCWNDSize;
        m_dLastCWNDSize = m_dCWndSize;
        m_CWNDMeasure.push(m_dCWndSize);

        // Measure the sender speed
        measureSenderSpeed();

        int number_loss, number_ack;
        double loss_rate;
        calculateLossData(Ref(loss_rate), Ref(number_loss), Ref(number_ack), last_ack, m_LastLossAckSeq);

        m_LossRateHistory.push(loss_rate);

        double timed_loss_rate = double(number_loss)/ack_period;
        m_dTimedLossRate = avg_iir<4>(m_dTimedLossRate, timed_loss_rate);

        // Alroght, clear the loss stats.
        m_UnackLossRange.clear();

        // During warmup time (barely possible, but still)

        uint64_t acked_rcv_time = 0;

        std::ostringstream rttform;

        // Extract the acked-to-ack delay from the ack data
        int ack_delay = acksize > ACKD_ACKDELAY ? ackdata[ACKD_ACKDELAY] : -1; // Also received ACKDELAY may be -1
        if (ack_delay != -1)
        {
            // Now we have the previous data, the difference can be calculated.
            int ack_span = CSeqNo::seqcmp(ad.ack, last_ack);
            uint64_t acked_rcv_period = ack_period - ack_delay;

            // ACK period is the time between the previous ACK and current ACK.
            // By ACK delay we know how much time elapsed between reception of
            // the ACK-ed packet and sending the ACK for it.
            //
            // So, the ACK period decreased by this time delay gives a time that
            // elapsed since last ACK and the moment when the newly ACK-ed packet
            // was received. Then we simply use the proportional equation:

            // acked_rcv_period     ack_period
            // ---------------- == ---------------
            // ack_span              ack_size (X)

            int ack_size = (number_loss == 0) // lost packets collected since last ACK
                ? ack_span : ack_span * (double(ack_period)/acked_rcv_period);

            // By having this value, we have a loss rate calculated with time-predicted
            // number of packets to be received during this ACK period.
            // If the ack_size is greated by more than twice the loss size,
            // ignore it, otherwise take the greated loss rate of these two.
            if (ack_size > 0 && ack_size < number_ack*2)
            {
                double number_loss_rate = double(number_loss)/ack_size;

                // XXX This calculation is wrong. Use it only for an extra log
                // loss_rate = max(loss_rate, number_loss_rate);
                HLOGC(mglog.Debug, log << "FlowSmoother: ACK: considered number_loss_rate="
                        << number_loss_rate << " vs. loss_rate=" << loss_rate);
            }

            // Order of events in time:
            //
            //   ad.send_time                    currtime
            //   |                               |
            //   sent     received    sent ACK   received ACK
            //     S->       ->D ...   A->         ->A
            //     \\--------//---------\\----------//
            //  snd duration  ack_delay   rcv duration
            //
            //  RTT = snd duration + rcv duration
            //     where
            //  snd duration + rcv duration + ack_delay = (curtime - ad.send_time)
            //
            // This calculates the time distance between NOW (the last item here)
            // and the send time (the first item here), with exclusion of the time
            // distance between reception of given packet and the time when the receiver
            // decided to send the ACK message for this packet. This 
            acked_rcv_time = currtime - ad.send_time - ack_delay;

            // this value is not to be calculated if the ack_delay could not
            // be delivered.

            // Presentation:
            rttform << "(" << (currtime - ad.send_time) << "-" << ack_delay << ")=" << acked_rcv_time;
        }
        else if (number_loss == 0)
        {
            // When we had no loss since last ACK, then in case of absence of ack_delay
            // we can state that it is negligible and we can still have a reliable measurement
            // of Sender RTT.
            acked_rcv_time = currtime - ad.send_time;
            rttform << "(directly)=" << acked_rcv_time;
        }
        else
        {
            // We can fall back to measuring the time of the last sequence number, find the
            // packet with that sequence number and get its sending time. We state here that
            // the "ack delay time" here is negligible.
            acked_rcv_time = currtime - m_parent->sndTimeOf(ackdata[ACKD_RCVLASTSEQ]);
            rttform << "(@snd." << ackdata[ACKD_RCVLASTSEQ] << ")=" << acked_rcv_time;
        }

        m_LossRateMeasure.update(loss_rate);
        bool loss_rate_reliable = m_LossRateMeasure.size() > 3;
        if (loss_rate_reliable)
        {
            m_dLossRate = m_LossRateMeasure.currentAverage();
        }

        HLOGC(mglog.Debug, log << "FlowSmoother: LOSSDATA: avgrate=" << m_dLossRate
                << "(" << (loss_rate_reliable ? "" : "UN") << "reliable) history "
                << Printable(m_LossRateHistory) << " average=" << m_LossRateHistory.average());

        // For COOLDOWN or KEEP state, exit the state if the no-loss situation
        // was achieved. 
        if (m_State.state == FS_COOLDOWN)
        {
            if (loss_rate_reliable && m_dLossRate == 0 && m_LossRateHistory.average() == 0)
            {
                // Exit COOLDOWN state as we no longer experience packet loss.
                HLOGC(mglog.Debug, log << "FlowSmoother: REACHED loss rate " << m_dLossRate
                        << " Average loss history: " << m_LossRateHistory.average() << " -> SPEED ANALYSIS");
                switch_time = true;
            }
        }

        if (m_dTimedLossRate == 0)
            m_dTimedLossRate = timed_loss_rate;
        else
            m_dTimedLossRate = avg_iir<4>(m_dTimedLossRate, timed_loss_rate);


        // Ok, we have a loss rate and sender speed.
        // Receiver velocity will be extracted from the data
        int32_t rcv_velocity = acksize > ACKD_RCVVELOCITY ? ackdata[ACKD_RCVVELOCITY] : 0;
        int32_t rcv_speed = acksize > ACKD_RCVSPEED ? ackdata[ACKD_RCVSPEED] : 0;
        if (rcv_velocity)
            m_LastRcvVelocity = rcv_velocity;
        if (rcv_speed)
        {
            m_LastRcvSpeed = rcv_speed;
            m_MaxReceiverSpeed_pps = std::max(m_MaxReceiverSpeed_pps, rcv_speed);
            if (m_LastGoodSndPeriod_us == 0)
                m_LastGoodSndPeriod_us = m_dPktSndPeriod;
        }

        // Zero means "not measured".
        //
        // And we need also the Sender RTT in order to have a delivery speed.
        if (acked_rcv_time)
        {
            m_SenderRTTMeasure.update(acked_rcv_time);
            m_SenderRTT_us = m_SenderRTTMeasure.currentAverage();
            m_LastSenderRTT = acked_rcv_time;
        }

        int32_t last_contig = CSeqNo::decseq(ad.ack);
        int32_t uack_span = CSeqNo::seqcmp(last_rcv, last_contig);
        int32_t ack_span = CSeqNo::seqlen(last_ack, last_contig);

        // Alright, we have the following data now:
        // - Sender Speed: m_SenderSpeed_pps (updated from the sender events)
        // - Receiver Velocity: rcv_velocity (as received from the receiver)
        // - Delivery Speed = 1/Sender RTT: m_SenderRTT_us (as calculated from sender RTT)
        // - Loss rate: loss_rate
        Probe probe = {
            m_dPktSndPeriod,
            m_SenderSpeed_pps,
            rcv_velocity,
            rcv_speed,
            m_SenderRTT_us,
            ack_span, uack_span,
            loss_rate,
            (m_dCWndSize - last_cwnd_size)/m_dCongestionWindow
        };

        // The current measurements are being made the average, if
        // the measurement is being made multiple times for the same probe index.
        // In case when m_zProbeSpan == 1, this will be equivalent to a simple assignment.
        m_adStats[m_State.probe_index].update(m_State.span_index, probe);

        HLOGC(mglog.Debug, log << "FlowSmoother: ACK STATS: {" << DisplayState(m_State) << "}"
                << " Snd: Period:" << m_dPktSndPeriod << "us"
                << " sRTT:" << rttform.str() << "(" << m_SenderRTT_us << ")"
                << "us Speed=" << m_SenderSpeed_pps << "p/s"
                << " ACKspan:[" << last_ack << "-" << last_contig << ">" << uack_span << "](" << ack_span << ")"
                << " Flight:" << flight_span << "p"
                << " CWND:" << setprecision(6) << std::fixed << m_dCongestionWindow
                << " RcvVelocity=" << rcv_velocity << "p/s RcvSpeed=" << rcv_speed << "(" << m_parent->deliveryRate() << ")"
                << "p/s" << " pRTT=" << m_parent->RTT() << "us BW=" << m_parent->bandwidth() << "p/s"
                << " LOSS n=" << number_loss
                << setprecision(6) << std::fixed
                << "p rate:" << loss_rate << "("
                << m_dLossRate << ")p/ack, freq:"
                << int(1000.0*m_dTimedLossRate) << "p/ms "
                << decision
                );

        ++m_State.span_index;
        if (m_State.span_index == m_zProbeSpan)
        {
            ++m_State.probe_index;
            m_State.span_index = 0;

            if (m_State.probe_index == m_zProbeSize)
            {
                // Reached end of probe size. Analyze results.
                HLOGC(mglog.Debug, log << "FlowSmoother: REACHED PROBE SIZE -> SPEED ANALYSIS");
                switch_time = true;
            }
        }

        if (switch_time)
        {
            // This shall always RESET THE INDEX.
            analyzeSpeed();
        }

        if (m_State.state == FS_BITE)
        {
            // Check the measurement stage of the congestion window.
            if (m_iCWNDMeasureStage == 0)
            {
                // This is the first part. The congestion window is now extremely high
                // and we expect it to stop dropping, that is, the first time the new
                // value is greater than the previous one. If so, switch to stage 1.
                if (m_dCongestionWindow > last_cwnd_size)
                {
                    HLOGC(mglog.Debug, log << "FlowSmoother: CWND GROWS BACK (" << last_cwnd_size
                            << " -> " << m_dCongestionWindow << ")");
                    m_iCWNDMeasureStage = 1;
                    if (m_dPktSndPeriod_SLOWEST_us == 0)
                        m_dPktSndPeriod_SLOWEST_us = m_dPktSndPeriod;
                }

                return;
            }

            /* Don't do any other measurements using congestion window.
               The "growing back" is the only reliable measurement of some event
               that still happened already in a far past and no reliable events
               can be found out from this measurement.

            if (m_iCWNDMeasureStage == 1)
            {
                // Now we are trying to catch the moment when it goes back down again.
                if (m_dCongestionWindow < last_cwnd_size)
                {
                    if (m_dPktSndPeriod_SLOWEST_us == 0 || m_dPktSndPeriod_FASTEST_us == 0)
                    {
                        // Now limit found yet. It must speed up until it reaches the loss.
                        HLOGC(mglog.Debug, log << "FlowSmoother: CWND falling down, but speed range not set: slowest="
                                << m_dPktSndPeriod_SLOWEST_us << " fastest=" << m_dPktSndPeriod_FASTEST_us);
                        m_iCWNDMeasureStage = 0;
                    }
                    else
                    {
                        // Current speed is the fastest and extraordinary.
                        // Now calculate the position of 3/4 of that
                        double slowsegment = (m_dPktSndPeriod_SLOWEST_us - m_dPktSndPeriod_FASTEST_us)/4;
                        if (slowsegment > 0) // you never know
                        {
                            // Alright, let's set this as the current speed and keep it
                            // constant for a while.
                            m_dPktSndPeriod = m_dPktSndPeriod_FASTEST_us + slowsegment;
                            HLOGC(mglog.Debug, log << "FlowSmoother: CWND falling down again; SNDPERIOD: "
                                    << m_dPktSndPeriod_SLOWEST_us << " ... " << m_dPktSndPeriod_FASTEST_us
                                    << " -> Selecting " << m_dPktSndPeriod << " to KEEP");
                            switchState(FS_KEEP, 16, 1, m_dPktSndPeriod_SLOWEST_us, m_dPktSndPeriod_FASTEST_us);
                            m_iCWNDMeasureStage = 2; // No more CWND measurement required
                        }
                    }
                }
                return;
            }

            // Decide switching to FS_KEEP state by getting the trigger of the
            // congestion window change
            // In the beginning of FS_BITE state the congestion window decreases,
            // up to some point, when it stops, and slightly goes back.

            if (m_dCongestionWindow > last_cwnd_size && m_CWNDMeasure.full() )
            {
                switchState(FS_KEEP, 32, 1, m_dPktSndPeriod, 0);
            }
            */
        }
    }

    template <class ValueType>
    std::string showStatsPoint(size_t index, ValueType Probe::*field)
    {
        std::ostringstream str;
        str << "[" << m_adStats[index].snd_period << "]"
            << m_adStats[index].*field;
        return str.str();
    }

    void analyzeSpeed()
    {
        // Ok, don't do really speed analysis.
        // Just keep the current conditions for stats - experimental
        unsigned last_index = m_State.probe_index;
        m_State.probe_index = 0;
        m_State.span_index = 0;

#ifdef ENABLE_HEAVY_LOGGING
        if (last_index)
            HLOGC(mglog.Debug, log << "SPEED STATS: SP[us]| Tx Veloc | Rx Veloc | Rx Speed |Snd RTT[ms]| Lossrate | Congestion Rate");
        for (unsigned i = 0; i < last_index; ++i)
        {
            Probe& p = m_adStats[i];
            HLOGF(mglog.Debug, " %17f | %8d | %8d | %8d | %9.6f | %8.6f | %f",
                    p.snd_period, int(p.tx_velocity), int(p.rx_velocity), int(p.rx_speed), double(p.sender_rtt/1000.0), p.lossrate, p.congestion_rate);
        }
#endif

        // Don't analyze the stats in this case.
        if (m_State.state == FS_BITE || m_State.state == FS_WARMUP)
            return;

        int t = calculateCongestionTendency(last_index);

        if (m_State.state == FS_COOLDOWN)
        {
            // COOLDOWN is like KEEP, just the difference is that we have switched
            // to this state because of high loss rate or edge-spike after warmup.
            // We believe then we have cooled down now and can restore the high speed.
            if (t > 0)
            {
                // Very unlikely, but if we still experience increasing loss,
                // this needs to slow down even more.
                m_dPktSndPeriod = m_dPktSndPeriod * 2;

                HLOGC(mglog.Debug, log << "FlowSmoother: COOLDOWN failed - increasing cooling with half a speed (period="
                        << m_dPktSndPeriod << "us)");
                switchState(FS_COOLDOWN, 8, 1, m_dPktSndPeriod, 0);
            }
            else
            {
                // No panic. We are in a clear situation. Or the loss rate is steady. :)
                //double best_speed = std::max(m_MaxReceiverSpeed_pps, m_parent->deliveryRate())*9/10;
                double best_speed = m_parent->deliveryRate();

                HLOGC(mglog.Debug, log << "FlowSmoother: COOLDOWN did the job. Setting to speed:"
                        << best_speed << "p/s with average=" << m_parent->deliveryRate()
                        << "p/s running=" << m_LastRcvSpeed << "p/s max=" << m_MaxReceiverSpeed_pps << "p/s");

                // Keep this speed constant for the next period and see what happens.
                // Keep the current speed as minimum.
                m_dPktSndPeriod_SLOWEST_us = m_dPktSndPeriod;
                m_dPktSndPeriod = 1000000.0/best_speed;
                switchState(FS_KEEP, 16, 1, m_dPktSndPeriod_SLOWEST_us, 0);
            }

        }
        else if (m_State.state == FS_KEEP)
        {
            // As an experiment, we'll calculate the measurements at this point, and then switch to FS_BITE state.
            // The FS_KEEP is always set with probe span == 1 (it wouldn't make sense otherwise),
            // so the value tendeny is calculated for the entire array.

            // Also with FS_KEEP the value of snd_period is ignored because it should be
            // equal to the current one in all probe cells. Therefore we are not interested
            // in the sender/receiver speed and velocity. We need to check two things:
            // - whether the loss rate increases
            // - whether the RTT increases

            // If so, it means that we are already sending too fast and we have to slowdown.
            if (t > 0)
            {
                /*
                double slowdown_factor = 1.125;
                m_dPktSndPeriod = ceil(m_dPktSndPeriod * slowdown_factor);
                HLOGC(mglog.Debug, log << "FlowSmoother: FS_KEEP, congestion increases, slowing down to " << m_dPktSndPeriod);

                // And measure again. Set the current sending speed as the fastest.
                switchState(FS_KEEP, 16, 1, 0, m_dPktSndPeriod);
                */

                HLOGC(mglog.Debug, log << "FlowSmoother: FS_KEEP, congestion increases, turn to FS_SLIDE with no lower limit");
                switchState(FS_SLIDE, 16, 4, m_dPktSndPeriod_SLOWEST_us, 0);
            }
            else
            {
                if (m_dPktSndPeriod_FASTEST_us != 0 && m_PrevState == FS_COOLDOWN)
                {
                    // Check if the previous state was COOLDOWN, in this case you can
                    // at the first time use the maximum ever noticed speed.
                    m_dPktSndPeriod_FASTEST_us = 1000000.0/m_MaxReceiverSpeed_pps;
                    HLOGC(mglog.Debug, log << "FlowSmoother: FS_KEEP, FASTEST period not set, setting slowest period by max speed: "
                            << m_dPktSndPeriod_FASTEST_us);
                }

                if (m_dPktSndPeriod_FASTEST_us != 0)
                {
                    // We have a highest possible speed defined, so climb to it
                    // If the results don't get worse, switch currently to FS_BITE
                    HLOGC(mglog.Debug, log << "FlowSmoother: FS_KEEP, congestion steady, FASTEST speed known, switching to FS_CLIMB");
                    switchState(FS_CLIMB, 16, 4, m_dPktSndPeriod_SLOWEST_us, m_dPktSndPeriod_FASTEST_us);
                }
                else
                {
                    // We don't have the speed range recognized yet, so switch to climbing blindly.
                    HLOGC(mglog.Debug, log << "FlowSmoother: FS_KEEP, congestion steady, FASTEST speed UNKNOWN, switching to FS_BITE");

                    // Before switching to BITE, remember the current speed
                    m_LastGoodSndPeriod_us = m_dPktSndPeriod;
                    switchState(FS_BITE, 32, 1, m_dPktSndPeriod_SLOWEST_us, 0);
                }

            }
        }
        else if (m_State.state == FS_CLIMB)
        {
            // We have the loss tendency already analyzed, now let's analyze
            // the following factors:
            // - the receiver speed
            // - the receiver velocity
            // - the relationship between the ACK size and UACK size

            analyzeStats(last_index);
        }
        else if (m_State.state == FS_SLIDE)
        {
            Reverse(m_adStats, m_adStats + last_index);
            analyzeStats(last_index);
        }

        // Clear the stats after use
        memset(&m_adStats, 0, sizeof(m_adStats));
    }

    void analyzeStats(size_t last_index)
    {
        if (last_index < 4)
        {
            LOGC(mglog.Error, log << "FlowSmoother: IPE: analyzeStats() requires that at least 4 probes are made, "
                    << last_index << " received");
            return;
        }
        // This is in CLIMB or SLIDE state, should be in the order
        // of increasing speed (that is, decreasing snd_period).

        std::vector<int64_t> speedval;
        std::transform(m_adStats, m_adStats + last_index,
                std::back_inserter(speedval),
                Probe::field(&Probe::rx_speed));

        // Now create the value that unifies the number of
        // acked packets with the number of uacked packets.
        // This should report the value that represent the
        // ack speed. We need also to get the best ack speed.

        std::vector<int> ackval;
        std::vector<int> ackavg;

        for (size_t i = 0; i < last_index; ++i)
        {
            ackval.push_back(m_adStats[i].ack_size - m_adStats[i].uack_size);
        }
        int ackrate = accumulate(ackval.begin(), ackval.end(), 0)/ackval.size();

        int ack1 = ackval[0];
        ackrate += (ack1 - ackrate)/2;

        for (size_t i = 0; i < last_index; ++i)
        {
            ackrate = avg_iir<8>(ackrate, ackval[i]);
            ackavg.push_back(ackrate);
        }

        // Analyze the sender speed
        pair<size_t, size_t> speed_extremum;
        int64_t speed_median, speed_variance;
        size_t speed_extrval;

        HLOGC(mglog.Debug, log << "FlowSmoother: Analyzing receiver SPEED tendency");
        int spd_tend = arrayTendency(speedval, &speed_extremum, &speed_extrval, &speed_median, &speed_variance);
#if ENABLE_HEAVY_LOGGING

        {
            std::ostringstream exinfo;
            if (speed_extremum.first == last_index)
                exinfo << "NONE";
            else if (speed_extremum.first == speed_extremum.second)
            {
                exinfo << "STRICT:" << showStatsPoint(speed_extremum.first, &Probe::rx_speed);
            }
            else
            {
                exinfo << "(" << showStatsPoint(speed_extremum.first, &Probe::rx_speed)
                    << "|" << showStatsPoint(speed_extrval, &Probe::rx_speed)
                    << "|" << showStatsPoint(speed_extremum.second, &Probe::rx_speed) << ")";
            }

            HLOGC(mglog.Debug, log << "... median=" << speed_median << " variance=" << speed_variance
                    << "extremum: " << exinfo.str());
        }
#endif

        if (spd_tend == 1)
        {
            // The speed is increasing, we are below the searched speed.
            // Check the speed extremum

            // Take the lower bound of the extremum as a minimum speed, and reset the
            // maximum speed.
            m_dPktSndPeriod = m_dPktSndPeriod_OPTIMAL_us = m_adStats[speed_extremum.first].snd_period;
            double fastest = 0;
            // Preserve the fastest speed if it still exceeds the highest found speed by more than
            // variance. Otherwise reset it.
            if (m_dPktSndPeriod_FASTEST_us < (m_dPktSndPeriod - (m_dPktSndPeriod - m_adStats[speed_extrval].snd_period)/2))
                fastest = m_dPktSndPeriod_FASTEST_us;

            HLOGC(mglog.Debug, log << "FlowSmoother: SPEED TENDENCY+ extremum around pos ["
                    << speed_extremum.first << "-" << speed_extremum.second << "] sender-period ["
                    << m_adStats[speed_extremum.first].snd_period << "-"
                    << m_adStats[speed_extremum.second].snd_period << "] upper speed period:"
                    << fastest);

            switchState(FS_CLIMB, 16, 4, m_dPktSndPeriod, fastest);
            return;
        }

        if (spd_tend == -1)
        {
            // The speed is decreasing
            // Set the initial speed as maximum and go downwards
            double fastest = m_dPktSndPeriod;
            m_dPktSndPeriod = m_adStats[0].snd_period;
            m_dPktSndPeriod_OPTIMAL_us = 0; // we don't know the optimum speed yet

            HLOGC(mglog.Debug, log << "FlowSmoother: SPEED TENDENCY- extremum around pos ["
                    << speed_extremum.first << "-" << speed_extremum.second << "] sender-period ["
                    << m_adStats[speed_extremum.first].snd_period << "-"
                    << m_adStats[speed_extremum.second].snd_period << "] upper speed period:"
                    << fastest);
            switchState(FS_SLIDE, 16, 4, 0, fastest);
            return;
        }

        // Otherwise try to pick up the local maximum speed
        size_t middle = speed_extremum.first + (speed_extremum.second - speed_extremum.first)/2;
        if (speed_extremum.second - speed_extremum.first > 0 // nonempty range
                //AND the value in the middle is greater than at the edge, meaning local maximum
                && m_adStats[speed_extremum.second].rx_speed < m_adStats[middle].rx_speed)
        {
            m_dPktSndPeriod = m_adStats[speed_extremum.first].snd_period;

            HLOGC(mglog.Debug, log << "FlowSmoother: SPEED TENDENCY- extremum around pos ["
                    << speed_extremum.first << "-" << speed_extremum.second << "] sender-period ["
                    << m_adStats[speed_extremum.first].snd_period << "-"
                    << m_adStats[speed_extremum.second].snd_period << "] upper speed period:"
                    << 0);
            // And keep it constant for a while.
            switchState(FS_KEEP, 16, 1, m_dPktSndPeriod, 0);
            return;
        }

        // Analyze the ack-uack rate
        pair<size_t, size_t> ack_extremum;
        int ack_median, ack_variance;
        size_t ack_extrval;
        HLOGC(mglog.Debug, log << "FlowSmoother: Analyzing ACK RATE tendency");
        int ack_tend = arrayTendency(ackavg, &ack_extremum, &ack_extrval, &ack_median, &ack_variance);

#if ENABLE_HEAVY_LOGGING

        {
            std::ostringstream exinfo;
            if (speed_extremum.first == last_index)
                exinfo << "NONE";
            else if (speed_extremum.first == speed_extremum.second)
            {
                exinfo << "STRICT:" << showStatsPoint(speed_extremum.first, &Probe::ack_size);
            }
            else
            {
                exinfo << "(" << showStatsPoint(speed_extremum.first, &Probe::ack_size)
                    << "|" << showStatsPoint(speed_extrval, &Probe::ack_size)
                    << "|" << showStatsPoint(speed_extremum.second, &Probe::ack_size) << ")";
            }

            HLOGC(mglog.Debug, log << "... median=" << speed_median << " variance=" << speed_variance
                    << " extremum: " << exinfo.str());
        }
#endif
        // Check only if it doesn't get worse.
        if (ack_tend != -1)
        {
            m_dPktSndPeriod = m_dPktSndPeriod_OPTIMAL_us = m_adStats[ack_extremum.first].snd_period;
            double fastest = 0;
            // Preserve the fastest speed if it still exceeds the highest found speed by more than
            // variance. Otherwise reset it.
            if (m_dPktSndPeriod_FASTEST_us < (m_dPktSndPeriod - (m_dPktSndPeriod - m_adStats[ack_extrval].snd_period)/2))
                fastest = m_dPktSndPeriod_FASTEST_us;

            HLOGC(mglog.Debug, log << "FlowSmoother: ACKRATE TENDENCY" << (ack_tend?"=":"+") << " extremum around pos ["
                    << speed_extremum.first << "-" << speed_extremum.second << "] sender-period ["
                    << m_adStats[speed_extremum.first].snd_period << "-"
                    << m_adStats[speed_extremum.second].snd_period << "] upper speed period:"
                    << fastest);

            switchState(FS_CLIMB, 16, 4, m_dPktSndPeriod, fastest);
            return;
        }

        // It's getting worse. Jump to the lowest speed so far and keep it for a while,
        // then decide what to do.
        m_dPktSndPeriod = m_adStats[0].snd_period;
        switchState(FS_KEEP, 16, 1, 0, m_dPktSndPeriod);
    }

    int calculateCongestionTendency(size_t last_index)
    {
        if (last_index < 4)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother::calculateCongestionTendency: time series too short for measurement, no tendency reporting");
            return 0;
        }
        // This function should trace the values of loss rate and RTT
        // in the subsequent probes and get the tendency.

        // First we take the loss rate.
        // 1. Calculate the median and variance.
        std::vector<double> values;
        std::transform(m_adStats, m_adStats + last_index,
                std::back_inserter(values), Probe::field(&Probe::lossrate));

        HLOGC(mglog.Debug, log << "FlowSmoother:calculateCongestionTendency: checking loss rate tendency");
        int loss_tendency = arrayTendency(values);

        if (loss_tendency > 0) // increasing
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: CONGESTION TENDENCY: loss increasing: " << loss_tendency);
            return loss_tendency;
        }

        values.clear();
        std::transform(m_adStats, m_adStats + last_index,
                std::back_inserter(values), Probe::field(&Probe::sender_rtt));

        HLOGC(mglog.Debug, log << "FlowSmoother:calculateCongestionTendency: checking RTT tendency");
        int rtt_tendency = arrayTendency(values);

        return rtt_tendency; // return as is
    }

    template <class TYPE, class Predicate>
    static pair<size_t, size_t> TraceExtremumRange(const vector<TYPE>& array, size_t size, size_t ext_pos, Predicate comp)
    {
        TYPE v = array[ext_pos];

        // 1. Forwards
        size_t ext_pos_fwd = ext_pos, i = ext_pos;
        for (;;)
        {
            ++i;
            //cout << "FWD POS: " << i << " value: " << array[i] << " prev: " << v <<  endl;
            if (i == size || comp(array[i], v))
            {
                //cout << "FWD END\n";
                break;
            }
            v = array[i];
            ext_pos_fwd = i;
        }
        //cout << "FWD END: " << ext_pos_fwd << endl;

        size_t ext_pos_bwd = i = ext_pos;
        v = array[ext_pos];

        // Check if loop
        if (i > 0)
        {
            for (;;)
            {
                --i;
                //cout << "BWD POS: " << i << " value: " << array[i] <<  " prev: " << v << endl;
                if (comp(array[i], v))
                {
                    //cout << "BWD END\n";
                    break;
                }

                ext_pos_bwd = i;
                if (i == 0)
                {
                    //cout << "BWD 0\n";
                    break;
                }
                v = array[i];
            }
            //cout << "BWD END: " << ext_pos_bwd << endl;
        }
        else
        {
            //cout << "BWD ALREADY 0\n";
        }

        return make_pair(ext_pos_bwd, ext_pos_fwd);
    }

    template <class Type>
    int arrayTendency(const std::vector<Type>& array,
            pair<size_t, size_t>* extremum_range = 0,
            size_t* extremum = 0,
            Type* ret_median = 0,
            Type* ret_variance = 0)
    {
        // This function works only for at least 4 items.
        if (array.size() < 4)
            return 0; // No tendency observed.

        RingLossyStack<Type, 3> avgtracer;
        std::vector<Type> averages;

        // First fill it with the first 4 values
        for (size_t i = 0; i < 3; ++i)
            avgtracer.push(array[i]);

        // Calculate the first shot average out of it.
        double median = avgtracer.average();
        averages.push_back(median); // Ix [0]
        averages.push_back(median); // Ix [1]
        for (size_t i = 3; i < array.size(); ++i)
        {
            // The index of avgtracer will be 1 more than
            // the required index for the averages. This
            // is 'i' index. Averages array will get one less.
            avgtracer.push(array[i]);
            averages.push_back(avgtracer.average());
        }
        // We have filled:
        // [0] and [1] in the beginning
        // then [2] ... [size-1] as per [3] ... [size].
        // We have one element lacking, but let's skip it.

        double minimum, maximum;
        minimum = maximum = array[0];
        size_t minimum_pos = 0, maximum_pos = 0;
        double earlier = median;
        double later = median;

        // Now start picking up the variance size for this first range
        double variance = 0;
        for (size_t i = 1; i < array.size()-1; ++i)
        {
            // Now since this point on, calculate the variance
            // together with running average.
            double value = array[i];

            // VARIANCE: distance between the current value and JUMPING average.
            variance = std::max(variance, fabs(averages[i] - value));
            if (value > maximum)
            {
                maximum_pos = i;
                maximum = value;
            }
            if (value < minimum)
            {
                minimum_pos = i;
                minimum = value;
            }
            median = (median*i + value)/(i+1);
            later = averages[i]; // Note the last jumping average
            HLOGC(mglog.Debug, log << "FlowSmoother: arrayTendency(trace): value=" << value
                    << " min=" << minimum << " max=" << maximum << " var=" << variance);
        }

        double valuespan = maximum - minimum;
        double avg_tend = later - earlier;

        if (ret_variance)
            *ret_variance = variance;

        if (ret_median)
            *ret_median = median;

        HLOGC(mglog.Debug, log << "FlowSmoother: arrayTendency: values <" << minimum
                << " ... " << maximum << " = " << valuespan << "> median=" << median
                << " variance=" << variance
                << " average span <" << earlier << " ... " << later << " = " << avg_tend << ">");

        if (extremum_range)
        {
            // Find the last local extremum range.
            // Find first the last minimum and last maximum and pick up whichever is later.
            if (maximum_pos > minimum_pos)
            {
                if (extremum)
                    *extremum = maximum_pos;
                *extremum_range = TraceExtremumRange(array, array.size(), maximum_pos, std::greater<Type>());
            }
            else
            {
                if (extremum)
                    *extremum = minimum_pos;
                *extremum_range = TraceExtremumRange(array, array.size(), minimum_pos, std::less<Type>());
            }
        }

        if (variance >= valuespan)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: arrayTendency: NONE - variance exceeds valuespan");
            return 0; // no clear tendency
        }

        // Check how the early median relates to the later jumping average.
        // If so, we may have either:
        // - a clear increasing tendency
        // - an unclear tendency
        // The latter if the variance is higher than their difference
        if (fabs(avg_tend) < variance)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: arrayTendency: NONE - variance exceeds average tendency");
            return 0; // Variance is too high to have a reliable tendency
        }

        // Also a clear tendency is shown when at least the later
        // average is higher to median by more than half of variance.
        double over_earlier = fabs(median - earlier);
        double over_later = fabs(later - median);
        if (over_later < (variance/2) && over_earlier < (variance/2))
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: arrayTendency: NONE - late ("
                   << over_later << ") & early (" << over_earlier << ") median > half variance");
            return 0;
        }

        HLOGC(mglog.Debug, log << "FlowSmoother: arrayTendency: result: " << avg_tend);

        return avg_tend > 0 ? 1 : avg_tend < 0 ? -1 : 0;
    }

    /*
    void lockTopSpeed()
    {
        if (m_dPktSndPeriod_SLOWEST_us == 0)
        {
            // No lowest speed yet, use the previous increased speed,
            // or the current speed, whichever is lower.
            // (Note that LO/HI refer to the sending speed terms, but
            // the variable keeps the value using unit INVERTED towards
            // the speed).
            m_dPktSndPeriod_FASTEST_us = std::max(m_dLastDecPeriod, m_dPktSndPeriod);
        }
        else
        {
            m_dPktSndPeriod_FASTEST_us = m_dPktSndPeriod_SLOWEST_us;
            m_dPktSndPeriod_SLOWEST_us = 0; // about to be measured anew
        }

        // Set the SLIDE state and reset the probe index.
        m_State = State(FS_SLIDE);
    }
    */

    void slowIncrease()
    {
        int mss = m_parent->MSS();
        int64_t B = (m_parent->bandwidth() - 1000000.0 / m_dPktSndPeriod);
        int64_t B9 = m_parent->bandwidth() / 9;
        if ((m_dPktSndPeriod > m_dLastDecPeriod) && (B > B9))
        {
            B = B9;
            HLOGC(mglog.Debug, log << "FlowSmoother: INCREASE: continued, using B=" << B);
        }
        else
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: INCREASE: initial, using B=" << B << " with BW/sndperiod="
                    << (m_parent->bandwidth() / m_dPktSndPeriod));
        }

        double inc = 0;
        double i_mss = 1.0/mss;

        if (B <= 0)
            inc = i_mss;
        else
        {
            // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
            // Beta = 1.5 * 10^(-6)

            inc = pow(10.0, ceil(log10(B * mss * 8.0))) * 0.0000015 / mss;

            if (inc < i_mss)
                inc = i_mss;
        }

        double new_period = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);

        HLOGC(mglog.Debug, log << "FlowSmoother: INCREASE factor: " << inc << " with 1/MSS=" << i_mss);

        m_dPktSndPeriod = new_period;
    }

    bool updateSndPeriod(uint64_t currtime, int32_t ack, ref_t<std::string> decision)
    {
        if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
        {
            *decision = "[PASS]";
            return false;
        }

        bool bite_slowdown = false;
        bool updated = false;
        bool first_ack_ever = false;

        m_LastRCTime = currtime;
        updateCongestionWindowSize(ack);

        // Increase rate only in "climb" state (when the rate gets
        // increased in order to pick up the maximum receiver speed 

        // This is currently the experimental version with only FS_BITE
        // after exiting FS_WARMUP, that is, it's still done the old way.
        if (m_State.state == FS_WARMUP)
        {
            *decision = "[WARMUP]";
            goto RATE_LIMIT;
        }

        first_ack_ever = m_LastAckTime == 0;
        if (first_ack_ever && m_State.state == FS_COOLDOWN)
        {
            // It means that the speed has been already cut by experiencing
            // the first loss before any ACK could be received. That was ok
            // to slowdown, but this time set the speed again as with ACK you
            // get a better measurement.
            setCooldown(false, "FIRST-ACK");
            *decision = "[REFIX]";
        }

        if (m_State.state == FS_BITE)
        {
            if (m_bLoss)
            {
                m_bLoss = false;
                *decision = "[WAIT]";
                bite_slowdown = true;

                goto RATE_LIMIT;
            }

            slowIncrease();
            updated = true;
        }
        else
        {
            // Default is FS_KEEP
            if (m_State.state == FS_COOLDOWN)
                *decision == "[COOLDOWN]";
            else
                *decision = "[KEEP]";
            std::string reason = "JUST KEEPING";

            // Do speed modifications only if span_index == 0 (that is,
            // when last measurement has switched to the next cell, which
            // has not been updated yet by any value).
            if (m_State.span_index == 0)
            {
                if (m_State.state == FS_CLIMB)
                {
                    *decision = "[CLIMB]";
                    // Check if the minimum and maximum speed has been defined.
                    // If so, then the speed should be increased by 1/N of the
                    // distance between slowest and fastest, where N is the probe size.

                    // Speed up (decrease the sender period) by 10% of the current value.
                    if (m_dPktSndPeriod_FASTEST_us != 0 && m_dPktSndPeriod - m_dPktSndPeriod_FASTEST_us > 1)
                    {
                        // Divide the range between the highest speed and current speed into 10.
                        // Take the 9/10 of the distance between current and max and set to the
                        // middle point between these values.

                        double distance = (m_dPktSndPeriod - m_dPktSndPeriod_FASTEST_us)*9.0/10;
                        HLOGC(mglog.Debug, log << "FlowSmoother: FAST SPEEDUP by " << (distance/2) << " -> " << (m_dPktSndPeriod - (distance/2)));
                        m_dPktSndPeriod -= distance/2;
                        reason = "climbing to fastest";
                    }
                    else
                    {
                        // Use the old-fashion speed up method from UDT.
                        slowIncrease();
                    }
                }
                else if (m_State.state == FS_SLIDE)
                {
                    *decision = "[SLIDE]";
                    if (m_dPktSndPeriod_SLOWEST_us != 0 && m_dPktSndPeriod_SLOWEST_us - m_dPktSndPeriod > 1)
                    {
                        double begin_speed = 1000000.0/m_dPktSndPeriod;
                        double end_speed = 1000000.0/m_dPktSndPeriod_SLOWEST_us;
                        double selected_speed = begin_speed + 9.0/10*(end_speed - begin_speed);

                        m_dPktSndPeriod = 1000000.0/selected_speed;
                        HLOGC(mglog.Debug, log << "FlowSmoother: SLIDE SLOWDOWN from " << begin_speed << " to " << end_speed
                                << " - setting " << selected_speed << "(" << m_dPktSndPeriod << "us)");
                        reason = "sliding to slowest";
                    }
                    else
                    {
                        // Slow down standard way, similar to what happens at LOSS with BITE.
                        double slowdown_factor = 1.125;
                        if (m_dLossRate > 0.125)
                            slowdown_factor = 1 + m_dLossRate;

                        m_dPktSndPeriod = m_dPktSndPeriod * slowdown_factor;
                        reason = "sliding by constant factor";
                    }

                }
            }

        }

RATE_LIMIT:

        showUpdateLog();

        //set maximum transfer rate
        if (m_llMaxBW)
        {
            // bandwidth_pktps = m_llMaxBW/MSS;
            // bandwidth_pktpus = bandwidth_pktpus/1000000.0
            // minSP = 1 / bandwidth_pktpus  [us/pkt]
            double minSP = 1000000.0 / (double(m_llMaxBW) / m_parent->MSS());
            if (m_dPktSndPeriod < minSP)
            {
                m_dPktSndPeriod = minSP;
                HLOGC(mglog.Debug, log << "FlowSmoother: BW limited to " << m_llMaxBW
                    << " - SLOWDOWN sndperiod=" << m_dPktSndPeriod << "us");
            }
        }

        if (bite_slowdown)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: BITE SLOWDOWN AFTER LOSS detected. Switch to step-climbing");
            switchState(FS_CLIMB, 16, 4, m_dPktSndPeriod, m_LastGoodSndPeriod_us);
        }

        return updated;
    }

    void showUpdateLog()
    {
#if ENABLE_HEAVY_LOGGING
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

        HLOGC(mglog.Debug, log << "FlowSmoother: UPD (state:"
            << DisplayState(m_State) << ") wndsize=" << m_dCongestionWindow
            << " sndperiod=" << m_dPktSndPeriod
            << "us BANDWIDTH USED:" << usedbw << " (limit: " << m_llMaxBW
            << ") SYSTEM BUFFER LEFT: " << udp_buffer_free);
#endif
    }

    size_t addLossRanges(TevSeqArray ar)
    {
        size_t nloss = 0;
        int32_t last_loss = ar.first[0] & ~LOSSDATA_SEQNO_RANGE_FIRST;
        for (size_t i = 0; i < ar.second; ++i)
        {
            uint32_t val = ar.first[i];
            if ( IsSet(val, LOSSDATA_SEQNO_RANGE_FIRST) )
            {
                val = SEQNO_VALUE::unwrap(val);
                ++i;
                if (i == ar.second) // fatal, but ignore
                    break;
                uint32_t vlast = ar.first[i];

                SeqRange r;
                r.set_including(val, vlast);

                // Add the sequence difference
                nloss += r.size;
                m_UnackLossRange.push_back(r);
                last_loss = vlast;
                HLOGC(mglog.Debug, log << "FlowSmoother: ... LOSS [" << r.begin << " - " << vlast
                        << "] (" << r.size << ")");
            }
            else
            {
                SeqRange r;
                r.set_one(val);
                ++nloss;
                m_UnackLossRange.push_back(r);
                last_loss = val;
                HLOGC(mglog.Debug, log << "FlowSmoother: ... LOSS [" << r.begin << "]");
            }
        }

        // last_loss: last of the currently reported lost packets.
        // last_loss+1: a really received packet that triggered the LOSSREPORT
        // last_loss+2: the ACK value which is the last good received + 1.
        int32_t loss_acked = CSeqNo::incseq(last_loss, 2);

        if (CSeqNo::seqcmp(loss_acked, m_LastLossAckSeq) > 0) // sanity check
        {
            m_LastLossAckSeq = loss_acked;
        }

        return nloss;
    }

    // This function extracts the loss information from the given array.
    // Any already present loss information will be ignored.
    // All new loss information is additionally collected and returned.
    size_t collectLossAndAck(TevSeqArray ar)
    {
        // Check the current state.
        int32_t first_loss = SEQNO_VALUE::unwrap(ar.first[0]);

        // Check first if this is already collected.
        // Enough to check the first one.
        for (size_t i = 0; i < m_UnackLossRange.size(); ++i)
        {
            if (first_loss == m_UnackLossRange[i].begin)
                return 0; // This is a NAKREPORT
        }

        // Stating this is not a NAKREPORT, we have a guarantee that
        // the loss sequence follows the last one, or it's the first one.

        // And add the loss range.
        // If this happened to be a NAKREPORT sent after a lost LOSSREPORT,
        // ignore packets that are received, but happen to be between
        // various lost packets.
        HLOGC(mglog.Debug, log << "FlowSmoother: collecting loss: ACK ["
                << CSeqNo::decseq(m_LastLossAckSeq) << " - "
                << CSeqNo::decseq(first_loss) << "] (" << CSeqNo::seqoff(m_LastLossAckSeq, first_loss) << ") ...");
        return addLossRanges(ar);
    }

    void calculateLossData(ref_t<double> loss_rate,
            ref_t<int> number_loss,
            ref_t<int> number_acked,
            int32_t ack_begin, int32_t ack_end)
    {
        // This collects all the lengths of the period that is yet
        // about to be ACK-ed, but lossreport has already collected
        // some fragmentary information

        int whole_range = CSeqNo::seqcmp(ack_end, ack_begin);

        if (whole_range <= 0 || m_UnackLossRange.empty())
        {
            *number_loss = 0;
            *number_acked = 0;
            *loss_rate = 0;

            HLOGC(mglog.Debug, log << "FlowSmoother: LOSS CALC (" << ack_begin << " ... " << ack_end << "): No loss observed");
            return;
        }

        // IT IS ASSUMED that anything reported in m_UnackLossRange
        // never predates ack_begin.
        //
        // It means that it should be enough to calculate the sizes
        // of all loss ranges, and take ack_begin...ack_end range
        // as the range of all reported sequences. Then
        // number_acked = whole_range - number_loss

        size_t nloss = 0;
        for (size_t i = 0; i < m_UnackLossRange.size(); ++i)
        {
            nloss += m_UnackLossRange[i].size;
        }

        *loss_rate = double(nloss)/whole_range;
        *number_loss = nloss;
        *number_acked = whole_range - nloss;

        HLOGC(mglog.Debug, log << "FlowSmoother: LOSS CALC (" << ack_begin << " ... " << ack_end
                << " = " << whole_range << ") Total: ACK=" << (*number_acked)
                << " LOSS=" << nloss << " LOSS RATE: " << (*loss_rate));
    }

    // When a lossreport has been received, it might be due to having
    // reached the available bandwidth limit. Slowdown to avoid further losses.
    void onLOSS_CollectLoss(ETransmissionEvent, TevSeqArray arg)
    {
        const int32_t* losslist = arg.first;
        size_t losslist_size = arg.second;

        // Sanity check. Should be impossible that TEV_LOSSREPORT event
        // is called with a nonempty loss list.
        if ( losslist_size == 0 )
        {
            LOGC(mglog.Error, log << "IPE: FlowSmoother: empty loss list!");
            return;
        }

        m_bLoss = true;
        collectLossAndAck(arg);
        double loss_rate;
        int number_loss, number_acked;
        calculateLossData(Ref(loss_rate), Ref(number_loss), Ref(number_acked), m_LastAckSeq, m_LastLossAckSeq);

        m_PrevState = m_State.state;
        double avg_loss_rate = avg_iir<4>(m_dLossRate, loss_rate);

        if (m_State.state == FS_WARMUP)
        {
            // Enter the keep state with probe size as for cooldown (min. 4)
            // Stop when probe size reached or when loss rate gets to stable 0.
            // Cool down even more if the loss rate is clearly increasing.
            setCooldown(true, "LOSS");
        }

        if (m_State.state != FS_SLIDE)
        {
            HLOGC(mglog.Debug, log << "FlowSmoother: LOSS, state {" << DisplayState(m_State) << "} - NOT decreasing speed YET.");
        }

        if (m_dPktSndPeriod > 5 && m_dPktSndPeriod_FASTEST_us > m_dPktSndPeriod) // less than 5 is "ridiculously high", so disregard it
        {
            m_dPktSndPeriod_FASTEST_us = m_dPktSndPeriod;
        }

        // Needed for old algo marked by FS_BITE
        int lossbegin = SEQNO_VALUE::unwrap(losslist[0]);

        // Do not undertake any action on the loss. This will be collected
        // and a decision taken later after statistics are collected.
        string decision = "WARMUP";
        if (m_State.state == FS_BITE)
        {
            //*  Old code from UDT, slowdown basing on some calculations and random conditions.

            // In contradiction to UDT, TEV_LOSSREPORT will be reported also when
            // the lossreport is being sent again, periodically, as a result of
            // NAKREPORT feature. You should make sure that NAKREPORT is off when
            // using FlowSmoother, so relying on SRTO_TRANSTYPE rather than
            // just SRTO_SMOOTHER is recommended.

            int loss_span = CSeqNo::seqcmp(lossbegin, m_iLastRCTimeSndSeq);

            double slowdown_factor = 1.125;
            /*
            if (prevstate == FS_WARMUP)
            {
                HLOGC(mglog.Debug, log << "FlowSmoother: exitting WARMUP, lossrate=" << (100*avg_loss_rate) << "%");
            }
            else
            {
                if (avg_loss_rate/2 < 0.125)
                {
                    slowdown_factor = 1 + avg_loss_rate/2;
                }
                HLOGC(mglog.Debug, log << "FlowSmoother: still BITE, lossrate="
                        << setprecision(6) << fixed
                        << (100*avg_loss_rate) << "%"
                        << " slowdown by " << (100*(slowdown_factor-1)) << "%" );
            }
            */

            if (loss_span > 0)
            {
                m_dLastDecPeriod = m_dPktSndPeriod;
                m_dPktSndPeriod = ceil(m_dPktSndPeriod * slowdown_factor);

                m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.875 + m_iNAKCount * 0.125);
                m_iNAKCount = 1;
                m_iDecCount = 1;

                m_iLastRCTimeSndSeq = m_parent->sndSeqNo();

                // remove global synchronization using randomization
                srand(m_iLastRCTimeSndSeq);
                m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
                if (m_iDecRandom < 1)
                    m_iDecRandom = 1;
                HLOGC(mglog.Debug, log << "FlowSmoother: LOSS:NEW: rand=" << m_iDecRandom
                        << " avg NAK:" << m_iAvgNAKNum
                        << ", sndperiod=" << m_dPktSndPeriod << "us");
                decision = "SLOW-OVER";
            }
            else if ((m_iDecCount ++ < 5) && (0 == (++ m_iNAKCount % m_iDecRandom)))
            {
                // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
                m_dPktSndPeriod = ceil(m_dPktSndPeriod * slowdown_factor);
                m_iLastRCTimeSndSeq = m_parent->sndSeqNo();
                HLOGC(mglog.Debug, log << "FlowSmoother: LOSS:PERIOD: loss_span=" << loss_span
                        << ", decrnd=" << m_iDecRandom
                        << ", sndperiod=" << m_dPktSndPeriod << "us");
                decision = "SLOW-STILL";
            }
            else
            {
                HLOGC(mglog.Debug, log << "FlowSmoother: LOSS:STILL: loss_span=" << loss_span
                        << ", decrnd=" << m_iDecRandom
                        << ", sndperiod=" << m_dPktSndPeriod << "us");
                decision = "KEEP-SPEED";
            }

            // If the decreased speed results to be slower than the current slowest,
            // set the slowest speed to this speed.
            if (m_dPktSndPeriod > m_dPktSndPeriod_SLOWEST_us)
                m_dPktSndPeriod_SLOWEST_us = m_dPktSndPeriod;
            // */

        }
        else
        {
            // In any other state than BITE, use only the emergency brake.
            // Remember the loss statistics, but don't react anyhow otherwise.

            // Make an average towards the current loss rate ( AVG(cur, cur, cur, loss_rate) )
            if (avg_iir<3>(m_dLossRate, loss_rate) > 1)
            {
                // Loss rate over 50%, emergency brake
                emergencyBrake();
                decision = "MRGC-BRAKE";
            }
            else
            {
                decision = "STAND-STILL";
            }
        }

        int32_t last_contig = CSeqNo::decseq(lossbegin);
        int32_t farthest_ack = CSeqNo::incseq(m_parent->sndSeqNo());
        int flight_span = CSeqNo::seqlen(last_contig, farthest_ack) - 1;

        int32_t last_rcv = (!m_UnackLossRange.empty())
            // If there is a loss, take the last loss and increase it.
            // This is the seq received that has triggered lossreport
            ? CSeqNo::incseq(m_UnackLossRange.back().end)
            // Otherwise place the last contiguous as a fallback.
            // Rather impossible because it would mean that a loss
            // report didn't report any loss :)
            : last_contig;

        HLOGC(mglog.Debug, log << "FlowSmoother: LOSS STATS: {" << DisplayState(m_State) << "}"
                << " Snd: Period:" << m_dPktSndPeriod << "us"
                << " sRTT:(last)=" << m_LastSenderRTT << "(" << m_SenderRTT_us << ")"
                << "us Speed=" << m_SenderSpeed_pps << "p/s"
                << " ACKspan:[" << m_LastAckSeq << "-"
                    << last_contig << ">" << CSeqNo::seqcmp(last_rcv, last_contig)
                    << "](0)" // Keep consistent format, but LOSS event could not increase ACK size here.
                << " Flight:" << flight_span << "p"
                << " CWND:" << setprecision(6) << std::fixed << m_dCongestionWindow
                << " RcvVelocity=" << m_LastRcvVelocity << "p/s RcvSpeed=" << m_LastRcvSpeed << "(" << m_parent->deliveryRate() << ")"
                << "p/s" << " pRTT=" << m_parent->RTT() << "us BW=" << m_parent->bandwidth() << "p/s"
                << " LOSS n=" << number_loss
                << setprecision(6) << std::fixed
                << "p rate:" << loss_rate << "("
                << m_dLossRate << ")p/ack, freq:"
                << int(1000.0*m_dTimedLossRate) << "p/ms "
                << "[" << decision << "]"
                );

    }

    void onTIMER(ETransmissionEvent, ECheckTimerStage stg)
    {
        // TEV_INIT is in the beginning of checkTimers(), used
        // only to synchronize back the values (which is done in updateCC
        // after emitting the signal).
        if (stg == TEV_CHT_INIT)
            return;

        HLOGC(mglog.Debug, log << "FlowSmoother: TIMER EVENT. State: " << DisplayState(m_State));

        ++m_zTimerCounter;

        // XXX Not sure about that, looxlike it will exit the WARMUP
        // before even reaching the top window size, or whichever comes first.
        if (m_State.state == FS_WARMUP)
        {
            // This is the very first update of sender speed.
            // Next sender speed update will be done at ACK events.
            if ( !m_parent->updateSenderSpeed(Ref(m_SenderSpeed_pps), Ref(m_SenderSpeed_bps)))
            {
                // Too early
                m_SenderSpeed_bps = 0;
                m_SenderSpeed_pps = 0;
            }
            else
            {
                // This is the last moment when the maximum sender speed can be notified
                // because the packets are being sent with highest possible speed. If there's
                // any slowdown applied to the sender speed, the sender speed will be slower than
                // the maximum possible anyway.
                m_MaxSenderSpeed_pps = m_SenderSpeed_pps;
            }
            setCooldown(false, "TIMER");
            return;
        }

        if (m_zTimerCounter > 32)
        {
            HLOGC(mglog.Warn, log << "32 CHECKTIMER events without ACK - resetting stats!");
            m_zTimerCounter = 0;

            measureSenderSpeed();
        }
    }

    Smoother::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return Smoother::SRM_LATEREXMIT;
    }
};


