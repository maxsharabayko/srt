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
SRT_MSGN_API extern int         srt_msgn_connect(const char *uri, size_t message_size);


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
SRT_MSGN_API extern int         srt_msgn_listen (const char *uri, size_t message_size);


/**
 * Receive a message.
 *
 * @param buffer        a buffer to send (has be less then the `message_size` used in srt_msngr_listen())
 * @param buffer_len    length of the buffer
 *
 * @return              number of bytes actually sent
 *                      -1 in case of error
 *                       0 in case all the connection are closed
 */
SRT_MSGN_API extern int         srt_msgn_send(const char *buffer, size_t buffer_len);


/**
 * Send a message.
 *
 * @param buffer        a buffer to send (has be less then the `message_size` used in srt_msngr_connect())
 * @param buffer_len    length of the buffer
 *
 * @return              number of bytes actually received
 *                      -1 in case of error
 *                       0 in case all the connections are closed
 */
SRT_MSGN_API extern int         srt_msgn_recv(char *buffer, size_t buffer_len);



SRT_MSGN_API extern const char* srt_msgn_getlasterror_str(void);


SRT_MSGN_API extern int         srt_msgn_getlasterror(void);


SRT_MSGN_API extern int         srt_msgn_destroy();


#ifdef __cplusplus
}   // extern "C"
#endif


#endif // INC__SRT_MESSENGER_H