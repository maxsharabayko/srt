/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__SRT_MESSENGER_H
#define INC__SRT_MESSENGER_H



#ifdef _WIN32
#define SRT_MSGN_API __declspec(dllexport)
#else
#define SRT_MSGN_API __attribute__ ((visibility("default")))
#endif  // _WIN32


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Establish SRT connection to a a remote host.
 *
 * @param uri           a null terminated string representing remote URI
 *                      (e.g. "srt://192.168.0.12:4200")
 * @param messahe_size  payload size of one message to send
 */
SRT_MSGN_API int         srt_msgn_connect(const char *uri, size_t message_size);


/**
 * Listen for the incomming SRT connections.
 * A non-blocking function.
 *
 * @param uri           a null terminated string representing local URI to listen on
 *                      (e.g. "srt://:4200" or "srt://192.168.0.12:4200?maxconn=10")
 * @param message_size  payload size of one message to send
 *
 * @return               0 on success
 *                      -1 on error
 */
SRT_MSGN_API int         srt_msgn_listen (const char *uri, size_t message_size);


/**
 * Send a message.
 *
 * @param[in] buffer        a buffer to send (has be less then the `message_size` used in srt_msngr_listen())
 * @param[in] buffer_len    length of the buffer
 *
 * @return                  number of bytes actually sent
 *                           0 in case of canceled send
 *                          -1 in case of error
 */
SRT_MSGN_API int         srt_msgn_send(const char *buffer, size_t buffer_len);


/**
 * Send a message on a specified connection ID.
 *
 * @param[in] buffer        a buffer to send (has be less then the `message_size` used in srt_msngr_listen())
 * @param[in] buffer_len    length of the buffer
 *
 * @return                  number of bytes actually sent
 *                           0 in case of canceled send
 *                          -1 in case of error
 */
SRT_MSGN_API int         srt_msgn_send_on_conn(const char *buffer, size_t buffer_len, int connection_id);


/**
 * Wait for all the outgoing data to be acknowledged by the receiver.
 * This ensures that the data is delivered, so the connection can be safely closed.
 *
 * @param[in] wait_ms       wait timeout in ms
 *                          -1 to wait forever
 *                           0 to check the state and return immediately
 *
 * @return                  number of bytes remain unacknowledged
 *                           0 in case there is no outgoing data unacknowledged
 *                          -1 in case of error
 */
SRT_MSGN_API int         srt_msgn_wait_undelievered(int wait_ms);


/**
 * Receive a message.
 *
 * @param[in,out]  buffer     a buffer to receive a message
 * @param[in]  buffer_len     length of the buffer
 * @param[out] connection_id  ID of the connection, for which the receive operation has hapened
 *
 * @return              number of bytes actually received
 *                       0 in case of canceled receive
 *                      -1 in case of error
 *                      -2 in case of unexpected error
 */
SRT_MSGN_API int         srt_msgn_recv(char *buffer, size_t buffer_len, int *connection_id);


struct SRTPerformanceStats
{
    int64_t  msTimeStamp;                // time since the UDT entity is started, in milliseconds
    int64_t  pktSentTotal;               // total number of sent data packets, including retransmissions
    int64_t  pktRecvTotal;               // total number of received packets
    int      pktSndLossTotal;            // total number of lost packets (sender side)
    int      pktRcvLossTotal;            // total number of lost packets (receiver side)
    int      pktRetransTotal;            // total number of retransmitted packets
    int      pktSentACKTotal;            // total number of sent ACK packets
    int      pktRecvACKTotal;            // total number of received ACK packets
    int      pktSentNAKTotal;            // total number of sent NAK packets
    int      pktRecvNAKTotal;            // total number of received NAK packets
    int64_t  usSndDurationTotal;         // total time duration when UDT is sending data (idle time exclusive)
    //>new
    int      pktSndDropTotal;            // number of too-late-to-send dropped packets
    int      pktRcvDropTotal;            // number of too-late-to play missing packets
    int      pktRcvUndecryptTotal;       // number of undecrypted packets
    uint64_t byteSentTotal;              // total number of sent data bytes, including retransmissions
    uint64_t byteRecvTotal;              // total number of received bytes

    uint64_t byteRcvLossTotal;           // total number of lost bytes
    uint64_t byteRetransTotal;           // total number of retransmitted bytes
    uint64_t byteSndDropTotal;           // number of too-late-to-send dropped bytes
    uint64_t byteRcvDropTotal;           // number of too-late-to play missing bytes (estimate based on average packet size)
    uint64_t byteRcvUndecryptTotal;      // number of undecrypted bytes
    //<

    // local measurements
    int64_t  pktSent;                    // number of sent data packets, including retransmissions
    int64_t  pktRecv;                    // number of received packets
    int      pktSndLoss;                 // number of lost packets (sender side)
    int      pktRcvLoss;                 // number of lost packets (receiver side)
    int      pktRetrans;                 // number of retransmitted packets
    int      pktRcvRetrans;              // number of retransmitted packets received
    int      pktSentACK;                 // number of sent ACK packets
    int      pktRecvACK;                 // number of received ACK packets
    int      pktSentNAK;                 // number of sent NAK packets
    int      pktRecvNAK;                 // number of received NAK packets
    double   mbpsSendRate;               // sending rate in Mb/s
    double   mbpsRecvRate;               // receiving rate in Mb/s
    int64_t  usSndDuration;              // busy sending time (i.e., idle time exclusive)
    int      pktReorderDistance;         // size of order discrepancy in received sequences
    double   pktRcvAvgBelatedTime;       // average time of packet delay for belated packets (packets with sequence past the ACK)
    int64_t  pktRcvBelated;              // number of received AND IGNORED packets due to having come too late
    //>new
    int      pktSndDrop;                 // number of too-late-to-send dropped packets
    int      pktRcvDrop;                 // number of too-late-to play missing packets
    int      pktRcvUndecrypt;            // number of undecrypted packets
    uint64_t byteSent;                   // number of sent data bytes, including retransmissions
    uint64_t byteRecv;                   // number of received bytes
    uint64_t byteRcvLoss;                // number of retransmitted bytes
    uint64_t byteRetrans;                // number of retransmitted bytes
    uint64_t byteSndDrop;                // number of too-late-to-send dropped bytes
    uint64_t byteRcvDrop;                // number of too-late-to play missing bytes (estimate based on average packet size)
    uint64_t byteRcvUndecrypt;           // number of undecrypted bytes
    //<

    // instant measurements
    double   usPktSndPeriod;             // packet sending period, in microseconds
    int      pktFlowWindow;              // flow window size, in number of packets
    int      pktCongestionWindow;        // congestion window size, in number of packets
    int      pktFlightSize;              // number of packets on flight
    double   msRTT;                      // RTT, in milliseconds
    double   mbpsBandwidth;              // estimated bandwidth, in Mb/s
    int      byteAvailSndBuf;            // available UDT sender buffer size
    int      byteAvailRcvBuf;            // available UDT receiver buffer size
    //>new
    double   mbpsMaxBW;                  // Transmit Bandwidth ceiling (Mbps)
    int      byteMSS;                    // MTU

    int      pktSndBuf;                  // UnACKed packets in UDT sender
    int      byteSndBuf;                 // UnACKed bytes in UDT sender
    int      msSndBuf;                   // UnACKed timespan (msec) of UDT sender
    int      msSndTsbPdDelay;            // Timestamp-based Packet Delivery Delay

    int      pktRcvBuf;                  // Undelivered packets in UDT receiver
    int      byteRcvBuf;                 // Undelivered bytes of UDT receiver
    int      msRcvBuf;                   // Undelivered timespan (msec) of UDT receiver
    int      msRcvTsbPdDelay;            // Timestamp-based Packet Delivery Delay
    //<
};


typedef struct SRTPerformanceStats SRT_PERF_STATS;

SRT_MSGN_API int         srt_msgn_bstats(SRT_PERF_STATS* stats, int connection_id, int clear);



SRT_MSGN_API const char* srt_msgn_getlasterror_str(void);


SRT_MSGN_API int         srt_msgn_getlasterror(void);


SRT_MSGN_API int         srt_msgn_set_loglevel(int loglevel, int verbose);


/**
 * Destroy the messaging service
 *
 * @return              0
 */
SRT_MSGN_API int         srt_msgn_destroy();


#ifdef __cplusplus
}   // extern "C"
#endif


#endif // INC__SRT_MESSENGER_H