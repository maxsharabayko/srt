/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */


#include "cc_copa.h"



void CopaCC::changeDirection(
    VelocityState::Direction newDirection,
    const TimePoint ackTime)
{
    if (m_velocityState.direction == newDirection)
        return;


    HLOGC(mglog.Debug, log << "CopaCC: Change direction to: " << newDirection);

    m_velocityState.direction = newDirection;
    m_velocityState.velocity = 1;
    m_velocityState.numTimesDirectionSame = 0;
    m_velocityState.lastCwndRecordTime = ackTime;
    m_velocityState.lastRecordedCwndBytes = cwndBytes_;
}



void CopaCC::onPacketAcked(ETransmissionEvent, EventVariant arg)
{
    const int ack = arg.get<EventVariant::ACK>();

    const uint64_t currtime = CTimer::getTime();

    SRT_ASSERT(bytesInFlight_ >= ack);
    bytesInFlight_ -= 




    subtractAndCheckUnderflow(bytesInFlight_, ack.ackedBytes);


    minRTTFilter_.Update(
        conn_.lossState.lrtt,
        std::chrono::duration_cast<microseconds>(ack.ackTime.time_since_epoch())
        .count());
    auto rttMin = minRTTFilter_.GetBest();
    standingRTTFilter_.SetWindowLength(conn_.lossState.srtt.count() / 2);
    standingRTTFilter_.Update(
        conn_.lossState.lrtt,
        std::chrono::duration_cast<microseconds>(ack.ackTime.time_since_epoch())
        .count());
    auto rttStandingMicroSec = standingRTTFilter_.GetBest().count();

    VLOG(10) << __func__ << "ack size=" << ack.ackedBytes
        << " num packets acked=" << ack.ackedBytes / conn_.udpSendPacketLen
        << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
        << " inflight=" << bytesInFlight_ << " rttMin=" << rttMin.count()
        << " sRTT=" << conn_.lossState.srtt.count()
        << " lRTT=" << conn_.lossState.lrtt.count()
        << " mRTT=" << conn_.lossState.mrtt.count()
        << " rttvar=" << conn_.lossState.rttvar.count()
        << " packetsBufferred="
        << conn_.flowControlState.sumCurStreamBufferLen
        << " packetsRetransmitted=" << conn_.lossState.rtxCount << " "
        << conn_;

    auto delayInMicroSec =
        duration_cast<microseconds>(conn_.lossState.lrtt - rttMin).count();
    if (delayInMicroSec < 0) {
        LOG(ERROR) << __func__
            << "delay negative, lrtt=" << conn_.lossState.lrtt.count()
            << " rttMin=" << rttMin.count() << " " << conn_;
        return;
    }
    if (rttStandingMicroSec == 0) {
        LOG(ERROR) << __func__ << "rttStandingMicroSec zero, lrtt = "
            << conn_.lossState.lrtt.count() << " rttMin=" << rttMin.count()
            << " " << conn_;
        return;
    }

    VLOG(10) << __func__
        << " estimated queuing delay microsec =" << delayInMicroSec << " "
        << conn_;

    bool increaseCwnd = false;
    if (delayInMicroSec == 0) {
        // taking care of inf targetRate case here, this happens in beginning where
        // we do want to increase cwnd
        increaseCwnd = true;
    }
    else {
        auto targetRate = (1.0 * conn_.udpSendPacketLen * 1000000) /
            (latencyFactor_ * delayInMicroSec);
        auto currentRate = (1.0 * cwndBytes_ * 1000000) / rttStandingMicroSec;

        VLOG(10) << __func__ << " estimated target rate=" << targetRate
            << " current rate=" << currentRate << " " << conn_;
        increaseCwnd = targetRate >= currentRate;
    }

    if (!(increaseCwnd && isSlowStart_)) {
        // Update direction except for the case where we are in slow start mode,
        checkAndUpdateDirection(ack.ackTime);
    }

    if (increaseCwnd) {
        if (isSlowStart_) {
            // When a flow starts, Copa performs slow-start where
            // cwnd doubles once per RTT until current rate exceeds target rate".
            if (!lastCwndDoubleTime_.hasValue()) {
                lastCwndDoubleTime_ = ack.ackTime;
            }
            else if (
                ack.ackTime - lastCwndDoubleTime_.value() > conn_.lossState.srtt) {
                VLOG(10) << __func__ << " doubling cwnd per RTT from=" << cwndBytes_
                    << " due to slow start"
                    << " " << conn_;
                addAndCheckOverflow(cwndBytes_, cwndBytes_);
                lastCwndDoubleTime_ = ack.ackTime;
            }
        }
        else {
            if (velocityState_.direction != VelocityState::Direction::Up &&
                velocityState_.velocity > 1.0) {
                // if our current rate is much different than target, we double v every
                // RTT. That could result in a high v at some point in time. If we
                // detect a sudden direction change here, while v is still very high but
                // meant for opposite direction, we should reset it to 1.
                changeDirection(VelocityState::Direction::Up, ack.ackTime);
            }
            uint64_t addition = (ack.ackedPackets.size() * conn_.udpSendPacketLen *
                conn_.udpSendPacketLen * velocityState_.velocity) /
                (latencyFactor_ * cwndBytes_);
            VLOG(10) << __func__ << " increasing cwnd from=" << cwndBytes_ << " by "
                << addition << " " << conn_;
            addAndCheckOverflow(cwndBytes_, addition);
        }
    }
    else {
        if (velocityState_.direction != VelocityState::Direction::Down &&
            velocityState_.velocity > 1.0) {
            // if our current rate is much different than target, we double v every
            // RTT. That could result in a high v at some point in time. If we detect
            // a sudden direction change here, while v is still very high but meant
            // for opposite direction, we should reset it to 1.
            changeDirection(VelocityState::Direction::Down, ack.ackTime);
        }
        uint64_t reduction = (ack.ackedPackets.size() * conn_.udpSendPacketLen *
            conn_.udpSendPacketLen * velocityState_.velocity) /
            (latencyFactor_ * cwndBytes_);
        VLOG(10) << __func__ << " decreasing cwnd from=" << cwndBytes_ << " by "
            << reduction << " " << conn_;
        isSlowStart_ = false;
        subtractAndCheckUnderflow(
            cwndBytes_,
            std::min<uint64_t>(
                reduction,
                cwndBytes_ -
                conn_.transportSettings.minCwndInMss * conn_.udpSendPacketLen));
    }
}



