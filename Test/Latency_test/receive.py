# -*- coding: utf-8 -*-
from scapy.all import *
import csv
import os
import struct
import time

interface_receive = "ens5f0"
target_port = 12345
BUFFER_SIZE = 100

def setup_logger():
    filename = f"timestamp_diff_{time.strftime('%Y%m%d_%H%M%S')}.csv"
    with open(filename, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.writer(f)
        writer.writerow([
            'SenderTimestamp', 
            'ReceiverTimestamp', 
            'Latency(ns)', 
            'SourceIP',
            'PacketSize'
        ])
    return filename

def packet_handler(packet, buffer, filename):
    if packet.haslayer(TCP) and packet[TCP].dport == target_port:
        try:
            raw_payload = bytes(packet[TCP].payload)
            if len(raw_payload) < 8:
                return

            # 发送方时间戳处理（8字节大端无符号整型）
            sender_bytes = raw_payload[:8]
            sender_ns = int.from_bytes(sender_bytes, byteorder='big', signed=False)
            
            # 接收方时间戳处理（双精度浮点转整型）
            recv_time = packet.time
            recv_bytes = struct.pack('!d', recv_time)
            recv_ns = int.from_bytes(recv_bytes, byteorder='big', signed=False)
            
            # 计算时延
            latency = recv_ns - sender_ns
            
            buffer.append([
                str(sender_ns),
                str(recv_ns),
                str(latency),
                packet[IP].src,
                len(raw_payload)
            ])
            
            if len(buffer) >= BUFFER_SIZE:
                with open(filename, 'a', newline='', encoding='utf-8-sig') as f:
                    writer = csv.writer(f)
                    writer.writerows(buffer)
                    buffer.clear()
                    print(f"Buffered {BUFFER_SIZE} records")

        except Exception as e:
            print(f"Error: {str(e)}")

def start_sniffing(filename):
    buffer = []
    print(f"\n[START] Capturing timestamp difference on {interface_receive}")
    try:
        sniff(iface=interface_receive,
              filter=f"tcp and dst port {target_port}",
              prn=lambda p: packet_handler(p, buffer, filename),
              store=False)
    finally:
        if buffer:
            with open(filename, 'a', newline='', encoding='utf-8-sig') as f:
                writer = csv.writer(f)
                writer.writerows(buffer)

if __name__ == "__main__":
    os.system(f"sudo ip link set {interface_receive} promisc on")
    csv_file = setup_logger()
    start_sniffing(csv_file)
    print("[COMPLETE] Data saved to:", csv_file)