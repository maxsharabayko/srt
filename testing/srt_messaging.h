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



SRT_MSGN_API const char* srt_msgn_getlasterror_str(void);


SRT_MSGN_API int         srt_msgn_getlasterror(void);


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