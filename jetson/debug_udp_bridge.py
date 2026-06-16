#!/usr/bin/env python3
"""
debug_udp_bridge — forwards ROS '/debug' (std_msgs/String) to the KPI dashboard(s)
over UDP.

How it handles "different viewer IPs": each dashboard registers by sending the
datagram b"KPIDBG-HELLO" to this node's UDP port (every ~3 s). We learn that
viewer's address and push every /debug line to ALL currently-registered viewers.
So team members can be on different/changing IPs — nothing is hardcoded here; the
dashboards only need THIS Jetson's IP (config.json -> debug.host, or env
KPI_DEBUG_HOST).

Run on the Jetson:
    python3 debug_udp_bridge.py                 # listens on UDP 45103

Publish debug from anywhere:
    ros2 topic pub --once /debug std_msgs/String "data: 'WARN: localization unstable'"
or from a node:
    self.dbg = self.create_publisher(String, '/debug', 10)
    self.dbg.publish(String(data='planner: path found'))     # -> INFO
    self.dbg.publish(String(data='ERROR: lidar timeout'))    # -> ERROR (colored red)

Level: if the message starts with DEBUG/INFO/WARN/ERROR/FATAL (optionally in [..]
and/or followed by ':'), that becomes the level; otherwise INFO.
"""
import re
import socket
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

UDP_PORT   = 45103          # must match the dashboard's debug.port
VIEWER_TTL = 12.0           # seconds without a HELLO before a viewer is forgotten
_LVL_RE = re.compile(
    r'^\s*\[?\s*(DEBUG|INFO|WARN|WARNING|ERROR|FATAL)\s*\]?\s*[:\-]?\s*(.*)$',
    re.IGNORECASE | re.DOTALL)


class DebugUdpBridge(Node):
    def __init__(self):
        super().__init__('debug_udp_bridge')
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.sock.bind(('0.0.0.0', UDP_PORT))
        self.viewers = {}                       # (ip, port) -> last_seen epoch
        self.create_subscription(String, '/debug', self.on_debug, 50)
        self.create_timer(0.2, self.poll_hellos)
        self.get_logger().info(
            f'debug_udp_bridge up: listening UDP {UDP_PORT}, forwarding /debug to viewers')

    def poll_hellos(self):
        now = time.time()
        while True:
            try:
                data, addr = self.sock.recvfrom(64)
            except (BlockingIOError, OSError):
                break
            if data.strip() == b'KPIDBG-HELLO':
                if addr not in self.viewers:
                    self.get_logger().info(f'viewer registered: {addr[0]}:{addr[1]}')
                self.viewers[addr] = now
        # prune viewers that stopped saying hello
        for a in [a for a, t in self.viewers.items() if now - t > VIEWER_TTL]:
            del self.viewers[a]

    def on_debug(self, msg):
        m = _LVL_RE.match(msg.data or '')
        if m:
            lvl = m.group(1).upper()
            lvl = 'WARN' if lvl == 'WARNING' else lvl
            text = m.group(2)
        else:
            lvl, text = 'INFO', (msg.data or '')
        payload = f'{lvl}|{text}'.encode('utf-8', 'replace')[:1400]   # keep < 1 MTU
        for addr in list(self.viewers):
            try:
                self.sock.sendto(payload, addr)
            except OSError:
                pass


def main():
    rclpy.init()
    node = DebugUdpBridge()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
