#pragma once
#ifndef INC__CC_COPA_H
#define INC__CC_COPA_H

#include <string>
#include <cmath>
#include <optional>


#include "common.h"
#include "core.h"
#include "queue.h"
#include "packet.h"
#include "congctl.h"


class CopaCC
    : public SrtCongestionControlBase
{
    typedef CopaCC Me; // Required by SSLOT macro

private:

    bool m_bSlowStart;
    uint64_t cwndBytes_;
    uint64_t bytesInFlight_{ 0 };


public:

    CopaCC(CUDT* parent)
        : SrtCongestionControlBase(parent)
        , m_bSlowStart(true)
        , cwndBytes_(10 * 1456)
    {
        parent->ConnectSignal(TEV_ACK,        EventSlot(this, &Me::onPacketAcked));
        parent->ConnectSignal(TEV_LOSSREPORT, EventSlot(this, &Me::slowdownSndPeriod));
        parent->ConnectSignal(TEV_CHECKTIMER, EventSlot(this, &Me::speedupToWindowSize));

        HLOGC(mglog.Debug, log << "Creating CopaCC");
    }

    bool checkTransArgs(SrtCongestion::TransAPI, SrtCongestion::TransDir, const char*, size_t, int, bool) ATR_OVERRIDE;

    bool needsQuickACK(const CPacket& pkt) ATR_OVERRIDE;

    void updateBandwidth(int64_t maxbw, int64_t) ATR_OVERRIDE;

private:

    typedef uint64_t TimePoint;

    struct VelocityState
    {
        uint64_t velocity{ 1 };
        enum Direction {
            None,
            Up, // cwnd is increasing
            Down, // cwnd is decreasing
        };
        Direction direction{ None };
        // number of rtts direction has remained same
        uint64_t numTimesDirectionSame{ 0 };
        // updated every srtt
        uint64_t lastRecordedCwndBytes;
        std::optional<TimePoint> lastCwndRecordTime{ std::nullopt };
    };

    VelocityState m_velocityState;

    void checkAndUpdateDirection(const TimePoint ackTime);
    void changeDirection(
        VelocityState::Direction newDirection,
        const TimePoint ackTime);


private:

    // SLOTS
    void onPacketAcked(ETransmissionEvent, EventVariant arg);

    // When a lossreport has been received, it might be due to having
    // reached the available bandwidth limit. Slowdown to avoid further losses.
    void slowdownSndPeriod(ETransmissionEvent, EventVariant arg);

    void speedupToWindowSize(ETransmissionEvent, EventVariant arg);

    SrtCongestion::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return SrtCongestion::SRM_LATEREXMIT;
    }
};


#endif INC__CC_COPA_H
