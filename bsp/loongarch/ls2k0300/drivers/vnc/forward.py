#!/usr/bin/env python3
# Copyright (C) 2026 OpenLANPU Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Description:
#   Host-only, bidirectional TCP forwarder for a board on a selected Linux NIC.
#   Keeps overlapping USB/Ethernet subnets from changing the VNC route. Does not
#   alter system routes. Listens only on loopback; not part of the RT firmware.

import argparse
import socket
import socketserver
import threading


def board_socket(host, port, interface):
    connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        connection.settimeout(10)
        if interface:
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE,
                                  interface.encode() + b'\0')
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        connection.connect((host, port))
        return connection
    except Exception:
        connection.close()
        raise


class Forwarder(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Connection(socketserver.BaseRequestHandler):
    def handle(self):
        remote = None
        stopped = threading.Event()

        def stop():
            stopped.set()
            for connection in (self.request, remote):
                if connection is not None:
                    try:
                        connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass

        def pump(source, destination):
            try:
                while not stopped.is_set():
                    try:
                        data = source.recv(32768)
                    except socket.timeout:
                        continue
                    if not data:
                        break
                    destination.sendall(data)
            except OSError:
                pass
            finally:
                stop()

        try:
            remote = board_socket(*self.server.destination)
            remote.settimeout(2)
            self.request.settimeout(2)
            self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            # Separate directions: a blocked framebuffer must not block input.
            receiver = threading.Thread(target=pump, args=(remote, self.request), daemon=True)
            receiver.start()
            pump(self.request, remote)
            receiver.join(timeout=3)
        except OSError as error:
            print(f'VNC forward: {error}', flush=True)
        finally:
            stop()
            if remote is not None:
                remote.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='Board IPv4 address')
    parser.add_argument('--port', type=int, default=5900, help='Board VNC port')
    parser.add_argument('--interface', help='Linux interface, for example eno1')
    parser.add_argument('--listen-port', type=int, default=5901, help='Loopback TCP port')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1024 <= args.listen_port <= 65535:
        parser.error('Invalid TCP port')
    try:
        with Forwarder(('127.0.0.1', args.listen_port), Connection) as server:
            server.destination = (args.host, args.port, args.interface)
            print(f'VNC forward 127.0.0.1:{args.listen_port} -> {args.host}:{args.port} '
                  f'via {args.interface or "system route"}', flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        return 0
    except OSError as error:
        parser.exit(1, f'VNC forward: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
