# SRT URI Syntax


## URI Query Options

+----------------------+-------------------+------------------------------+
| URI Query            | Values            | Description                  |
| (extension)          |                   |                              |
+======================+===================+==============================+
| `mode`               | - caller/client   |                              |
|                      | - listener/server |                              |
|                      | - rendezvous      |                              |
+----------------------+-------------------+------------------------------+
| `adapter`            | IP                |                              |
+----------------------+-------------------+------------------------------+

| URI param            | Values           | SRT Option                | Description |
| -------------------- | ---------------- | ------------------------- | ----------- |
| `congestion`         | {`live`, `file`} | `SRTO_CONGESTION`         | Type of congestion control. |
| `conntimeo`          | `ms`             | `SRTO_CONNTIMEO`          | Connection timeout. |
| `drifttracer`        | `bool`           | `SRTO_DRIFTTRACER`        | Enable drift tracer. |
| `enforcedencryption` | `bool`           | `SRTO_ENFORCEDENCRYPTION` | Reject connection if parties set different passphrase. |
| `fc`                 | `bytes`          | `SRTO_FC`                 | Flow control window size. |
| `groupconnect`       | {`0`, `1`}       | `SRTO_GROUPCONNECT`       | Accept group connections. |
| `groupstabtimeo`     | `ms`             | `SRTO_GROUPSTABTIMEO`     | Group stability timeout. |
| `inputbw`            | `bytes`          | `SRTO_INPUTBW`            | Input bandwidth. |
| `iptos`              | 0..255           | `SRTO_IPTOS`              | IP socket type of service |
| `ipttl`              | 1..255           | `SRTO_IPTTL`              | Defines IP socket "time to live" option. |
| `ipv6only`           | -1..1            | `SRTO_IPV6ONLY`           | Allow only IPv6. |
| `kmpreannounce`      | 0..              | `SRTO_KMPREANNOUNCE`      | Duration of Stream Encryption key switchover (in packets). |
| `kmrefreshrate`      | 0..              | `SRTO_KMREFRESHRATE`      | Stream encryption key refresh rate (in packets). |
| `latency`            | 0..              | `SRTO_LATENCY`            | Defines the maximum accepted transmission latency. |
| `linger`             | 0..              | `SRTO_LINGER`             | Link linger value |
| `lossmaxttl`         | 0..              | `SRTO_LOSSMAXTTL`         | Packet reorder tolerance. |
| `maxbw`              | 0..              | `SRTO_MAXBW`              | Bandwidth limit in bytes |
| `messageapi`         | `bool`           | `SRTO_MESSAGEAPI`         | Enable SRT message mode. |
| `minversion`         | maj.min.rev      | `SRTO_MINVERSION`         | Minimum SRT library version of a peer. |
| `mss`                | 76..             | `SRTO_MSS`                | MTU size |
| `nakreport`          | `bool`           | `SRTO_NAKREPORT`          | Enables/disables periodic NAK reports |
| `oheadbw`            | 5..100           | `SRTO_OHEADBW`            | limits bandwidth overhead, percents |
| `packetfilter`       | `string`         | `SRTO_PACKETFILTER`       | Set up the packet filter. |
| `passphrase`         | `string`         | `SRTO_PASSPHRASE`         | Password for the encrypted transmission. |
| `payloadsize`        | 0..              | `SRTO_PAYLOADSIZE`        | Maximum payload size. |
| `pbkeylen`           | {16, 24, 32}     | `SRTO_PBKEYLEN`           | Crypto key length in bytes. |
| `peeridletimeo`      | `ms`             | `SRTO_PEERIDLETIMEO`      | Peer idle timeout. |
| `peerlatency`        | `ms`             | `SRTO_PEERLATENCY`        | Minimum receiver latency to be requested by sender. |
| `rcvbuf`             | `bytes`          | `SRTO_RCVBUF`             | Receiver buffer size |
| `rcvlatency`         | `ms`             | `SRTO_RCVLATENCY`         | Receiver-side latency. |
| `retransmitalgo`     | {`0`, `1`}       | `SRTO_RETRANSMITALGO`     | Packet retransmission algorithm to use. |
| `sndbuf`             | `bytes`          | `SRTO_SNDBUF`             | Sender buffer size. |
| `snddropdelay`       | `ms`             | `SRTO_SNDDROPDELAY`       | Sender's delay before dropping packets. |
| `streamid`           | `string`         | `SRTO_STREAMID`           | Stream ID (settable in caller mode only, visible on the listener peer). |
| `tlpktdrop`          | `bool`           | `SRTO_TLPKTDROP`          | Drop too late packets. |
| `transtype`          | {`live`, `file`} | `SRTO_TRANSTYPE`          | Transmission type |
| `tsbpdmode`          | `bool`           | `SRTO_TSBPDMODE`          | Timestamp-based packet delivery mode. |



+------------+------------+-----------+
| Header 1   | Header 2   | Header 3  |
+============+============+===========+
| body row 1 | column 2   | column 3  |
+------------+------------+-----------+
| body row 2 | Cells may span columns.|
+------------+------------+-----------+
| body row 3 | Cells may  | - Cells   |
+------------+ span rows. | - contain |
| body row 4 |            | - blocks. |
+------------+------------+-----------+
