#!/usr/bin/env python3
"""fake_daemon.py — a fake imud server for client development. No hardware,
no imud install needed; Python 3 stdlib only.

Emits valid imud wire-v17 packets (276 bytes, little-endian, CRC32) with a
slowly rotating heading, mimicking the real daemon's [stream] TCP listener
and/or high-rate UDP output:

  ./fake_daemon.py                          # TCP listener on 0.0.0.0:10112
  ./fake_daemon.py --udp 192.168.1.42:10111 # ... plus UDP unicast
  ./fake_daemon.py --udp 239.255.0.1:10111  # ... plus UDP multicast
  ./fake_daemon.py --rate 100               # packet rate (default 10 Hz)

Contract mimicked from the real server (netserv):
  - broadcast-only: the server never reads from TCP clients
  - max 8 TCP clients; the 9th is accepted then immediately closed
  - whole-frame writes; a client whose buffer is full skips frames
    (imu_seq gaps), a client that errors/hangs up is closed
  - on Ctrl-C: one final packet with the SHUTDOWN flag, then FIN

Copyright (c) 2026 Richard Simpson
SPDX-License-Identifier: MIT
"""
import argparse
import errno
import math
import select
import socket
import struct
import sys
import time
import zlib

MAGIC = 0x494D5544
VERSION = 17
PACKET_SIZE = 276
_STRUCT = struct.Struct('<IHHQQII' + 'f' * 37 + 'I' + 'f' * 22 + 'I')
assert _STRUCT.size == PACKET_SIZE

FLAG_MAG_VALID = 1 << 0
FLAG_CONVERGED = 1 << 2
FLAG_ACCEL_CAL = 1 << 3
FLAG_GYRO_CAL = 1 << 4
FLAG_MAG_CAL = 1 << 5
FLAG_DECL_VALID = 1 << 10
FLAG_SHUTDOWN = 1 << 9
FLAGS = (FLAG_MAG_VALID | FLAG_CONVERGED | FLAG_ACCEL_CAL | FLAG_GYRO_CAL |
         FLAG_MAG_CAL | FLAG_DECL_VALID)

MAX_CLIENTS = 8
DECLINATION = 11.25


def make_packet(seq: int, heading: float, flags: int) -> bytes:
    now = time.time_ns()
    yaw = math.radians(heading if heading < 180.0 else heading - 360.0)
    f = [0.0] * 37          # accel..cov float run; quat starts at index 18
    f[2] = -9.81            # accel_z: at rest, NED
    f[5] = -9.81            # accel_raw_z
    f[18] = math.cos(yaw / 2)   # quat_w — yaw-only attitude
    f[21] = math.sin(yaw / 2)   # quat_z
    f[24] = yaw             # yaw, rad
    f[25] = heading         # heading_deg
    f[26] = 30.0            # rate_of_turn, deg/min (matches 0.5 deg/s sweep)
    f[27] = 25.5            # temp_c
    tail = [0.0] * 22       # declination..nis_mag float run
    tail[0] = DECLINATION
    # v17 gate-health/NIS block: the healthy case, so a consumer testing
    # against this sees "filter fine", not a fault reading.
    tail[18] = 1.0          # innov_weight — Huber cap never engaged
    tail[19] = 0.0          # innov_reject — nothing gate-rejected
    tail[20] = 1.0          # nis_accel — covariance consistent
    tail[21] = 1.0          # nis_mag
    body = _STRUCT.pack(MAGIC, VERSION, flags, now, now + 37_000_000_000,
                        (seq * 400) & 0xFFFFFFFF, 1, *f, seq, *tail, 0)
    body = body[:PACKET_SIZE - 4]
    return body + struct.pack('<I', zlib.crc32(body) & 0xFFFFFFFF)


def tcp_broadcast(clients: list, pkt: bytes) -> None:
    for c in clients[:]:
        try:
            n = c.send(pkt)
            if 0 < n < len(pkt):        # partial frame: close, like netserv
                raise OSError(errno.EPIPE, 'partial write')
        except OSError as e:
            if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                continue                # slow client: skip frame, keep it
            c.close()
            clients.remove(c)
            print(f'client dropped ({len(clients)} left)')


def main() -> int:
    ap = argparse.ArgumentParser(description='fake imud packet server')
    ap.add_argument('--tcp-port', type=int, default=10112)
    ap.add_argument('--no-tcp', action='store_true')
    ap.add_argument('--udp', metavar='HOST:PORT',
                    help='also send UDP (multicast if HOST is 224.0.0.0/4)')
    ap.add_argument('--rate', type=float, default=10.0, help='Hz (default 10)')
    args = ap.parse_args()

    listener = None
    if not args.no_tcp:
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('0.0.0.0', args.tcp_port))
        listener.listen(4)
        listener.setblocking(False)
        print(f'TCP listener on 0.0.0.0:{args.tcp_port}')

    udp_sock = udp_dest = None
    if args.udp:
        host, _, port = args.udp.rpartition(':')
        udp_dest = (host, int(port))
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        first_octet = int(host.split('.')[0]) if host[0].isdigit() else 0
        if 224 <= first_octet <= 239:
            udp_sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        else:
            udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        print(f'UDP to {host}:{port}')

    clients: list = []
    seq = 0
    heading = 0.0
    period = 1.0 / args.rate
    try:
        while True:
            rd = [listener] if listener else []
            readable, _, _ = select.select(rd, [], [], period)
            if listener in readable:
                for _ in range(16):
                    try:
                        conn, addr = listener.accept()
                    except OSError:
                        break
                    if len(clients) >= MAX_CLIENTS:
                        conn.close()    # like netserv: accept, then EOF
                        print(f'client {addr} rejected (server full)')
                        continue
                    conn.setblocking(False)
                    clients.append(conn)
                    print(f'client {addr} connected ({len(clients)})')
            pkt = make_packet(seq, heading, FLAGS)
            tcp_broadcast(clients, pkt)
            if udp_sock:
                udp_sock.sendto(pkt, udp_dest)
            seq += 1
            heading = (heading + 0.5 * period) % 360.0  # 0.5 deg/s sweep
    except KeyboardInterrupt:
        print('\nshutting down: sending SHUTDOWN-flag packet')
        pkt = make_packet(seq, heading, FLAGS | FLAG_SHUTDOWN)
        tcp_broadcast(clients, pkt)
        if udp_sock:
            udp_sock.sendto(pkt, udp_dest)
        for c in clients:
            c.close()
        if listener:
            listener.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
