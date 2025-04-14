# -*- coding: utf-8 -*-
from scapy.all import *
import time
import os
import struct

interface_send = "ens5f1"
target_ip = "11.0.0.2"
target_port = 12345

def send_packets():
    print(f"\nStarting to send packets on {interface_send}...")
    
    # 严格保持时间戳原始字节的发送逻辑
    for seq in range(100):
        # 获取精确发送时间戳（不进行任何转换）
        raw_timestamp = time.time()  # 保持原始浮点数精度
        
        # 构造不可变字节流（关键修改）
        payload = struct.pack('!d', raw_timestamp) + f"|SEQ{seq}|".encode('utf-8')
        
        # 构造最简数据包（移除flags干扰）
        packet = Ether()/IP(dst=target_ip)/TCP(dport=target_port)/payload
        
        # 记录精确发送时间（纳秒级时间源）
        send_ns = time.time_ns()  
        sendp(packet, iface=interface_send, verbose=False)
        
        # 验证字节完整性
        if len(payload) != 8 + len(f"|SEQ{seq}|"):
            print(f"Payload corruption at seq {seq}!")
            
        # 实时写入发送日志
        with open("send_log.bin", "ab") as f:
            f.write(struct.pack('!Q', send_ns))  # 写入8字节发送时间
            f.write(payload)  # 写入原始数据包内容
    
    print("\nAll 100 packets sent with raw timestamps!")

if __name__ == "__main__":
    os.system(f"ip link set {interface_send} up")
    send_packets()