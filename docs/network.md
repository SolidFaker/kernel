# Networking

## e1000

`drivers/net.c` drives QEMU's e1000:

- PCI scan for `8086:100E/100F`, MMIO BAR mapped at `0xC8000000`
- bus mastering + memory space enabled
- MAC read from RAL/RAH with a fallback MAC
- RX/TX descriptor rings and 2 KiB buffers allocated via `page_alloc`
- polling RX/TX, no interrupts

Guest address: `10.0.2.15/24`, gateway: `10.0.2.2`.

## ARP / IPv4 / ICMP

- ARP request/reply cache for next-hop MACs
- IPv4 header construction and checksum
- ICMP echo request/reply
- shell command: `ping` → `10.0.2.2`

## UDP

- `net_send_udp` builds Ethernet + IPv4 + UDP and sends through e1000
- `net_poll` parses incoming UDP and delivers datagrams to bound AF_INET `SOCK_DGRAM`
  sockets
- `poll`/`recvfrom` paths call `net_poll` so blocking waits make progress
- shell `net` runs an echo test to `10.0.2.2:9999`

## TCP client

Minimal client in `fs/socket.c` + `drivers/net.c`:

- active open (SYN/SYN-ACK/ACK), sequence/ack tracking
- PSH data send with ACK wait
- receive into the socket buffer, ACK, FIN handling
- `connect/send/recv/shutdown` over e1000
- `net` also tests `10.0.2.2:9998`

Host echo servers:

```sh
python3 -u /tmp/echo_servers.py   # UDP 9999 + TCP 9998
```

Limits: no retransmission/SACK/out-of-order reassembly, no DNS resolver, no IPv6, and
no real-network listening sockets. AF_INET loopback and AF_UNIX remain available.
