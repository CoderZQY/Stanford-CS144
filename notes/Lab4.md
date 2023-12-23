# Lab Checkpoint 4: down the stack (the network interface)

## 0. Overview

<img src="assets/The network interface.png" alt="The network interface" style="zoom:50%;" />

​	Lab4 要求实现网络接口部分，打通网络数据报（Internet datagrams）和链路层的以太网帧（link-layer Ethernet frames）之间的桥梁。之前的实验实现了 TCP segments 在使用 TCP 协议的设备之间的传输，而 TCP 报文有三种方式可被传送至远程服务器：

- **TCP-in-UDP-in-IP**：TCP 报文会被置于用户的数据报的 payload 中，在用户空间下这是最简单的实现方式：Linux 提供接口（如 UDPSocket），而用户仅需要提供 payload，目标地址，Linux 内核会负责将 UDP 报部，IP 报头，以太网报头组装起来，将这个网络包发向下一个 hop。**Linux 内核会保证每个 socket 具有独占的本地与远端地址以及端口号，并且保证这些数据在应用层的相互隔离。**

- **TCP-in-IP**：一般情况下，TCP 报文会直接放在 Internet datagrams 中，这通常被成为 "TCP/IP"。Linux 会提供一个 TUN 设备接口，需要应用层提供整个 Internet datagram，而 Linux 内核则会处理剩下的部分。但此时应用层需要自己构建整个 IP 报头以及 payload 部分。

- **TCP-in-IP-in-Ethernet**：以上的方法依赖 Linux 内核来实现的协议栈操作，每次用户向 TUN 设备写入 IP datagrams 时，Linux 都需要构建正确的带有 IP datagrams 的以太网帧作为 payload。这意味着 Linux 需要知悉下一跳的 IP 地址对应的以太网目的地址，否则 Linux 会以广播的形式请求这些信息："Who claims the following IP address ? "、"What’s your Ethernet address ? "

​	这些功能是由 Network Interface 实现的，该组件能将 IP 数据报转义成以太网帧等等，之后会传入 TAP 设备（类似 TUN 设备但更底层），实现对 link-layer 的数据帧的传输。

​	网络接口的大部分工作是：**为每个下一跳 IP 地址查找（和缓存）对应的以太网地址**。而对应的协议被称为：**地址解析协议 ARP (Address Resolution Protocol)**。

## 2. Checkpoint 4: The Address Resolution Protocol

​	Lab4 Network Interface 的主要任务：维护一个 IP 地址到 Ethernet 地址的映射表。这个映射类似缓存，或称作 "soft state"，能提高网络栈的传输效率。

1. `void NetworkInterface::send_datagram(const InternetDatagram &dgram, constAddress &next_hop);` 该方法被 TCPConnection 或者 router 所调用，这个接口就是将待发送的 Internet (IP) datagrams 转换成以太网帧并最终发送出去。

   - 如果以太网目的地址已知就直接发送，创建以太网帧（`type = EthernetHeader::TYPE_IPv4`），将 payload 设置为串行的数据报文，并设置源地址和目标地址。

   - 如果以太网目的地址未知，广播下一跳的以太网地址的 ARP 请求，并将 IP 报文放入队列中待 ARP 回复收到后将其发送出去。

   **注意事项：**需要**间隔 $5$ 秒**再发送相同的 ARP 请求，并且只有在收到目的以太网地址后再将数据报放入队列中。若没有收到目的以太网地址，需要将 datagram 以及其对应的 Address 都暂存在列表中

2. `optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame& frame);` 该方法接收来自网络的以太网帧，但需要忽略任何目的地址非网络接口部分的帧 （也就是只接受**广播地址**或者**接口自身的以太网地址**）。

   - 若为 IPv4 帧就将其以 InternetDatagram 进行解析，若成功则将解析的 InternetDatagram 返回给调用者。

   - 若为 ARP 帧就将其以 ARPMessage 进行解析，若成功则**缓存发送方 IP 地址与以太网帧的映射 30 秒**。若这个 ARP 请求是询问我们的 IP 地址，就发送正确的 ARP 答复。

3. `std::optional<EthernetFrame> maybe_send();` 在必要时发送 EthernetFrame。

4. `void NetworkInterface::tick(const size_t ms_since_last_tick);` 记录时间，使得任何已经过期的 IP 地址到 Ethernet 地址的映射失效。


​	整体代码如下：

- `network_interface.hh`

```c++
#pragma once

#include "address.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"

#include <iostream>
#include <list>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>

// A "network interface" that connects IP (the internet layer, or network layer)
// with Ethernet (the network access layer, or link layer).

// This module is the lowest layer of a TCP/IP stack
// (connecting IP with the lower-layer network protocol,
// e.g. Ethernet). But the same module is also used repeatedly
// as part of a router: a router generally has many network
// interfaces, and the router's job is to route Internet datagrams
// between the different interfaces.

// The network interface translates datagrams (coming from the
// "customer," e.g. a TCP/IP stack or router) into Ethernet
// frames. To fill in the Ethernet destination address, it looks up
// the Ethernet address of the next IP hop of each datagram, making
// requests with the [Address Resolution Protocol](\ref rfc::rfc826).
// In the opposite direction, the network interface accepts Ethernet
// frames, checks if they are intended for it, and if so, processes
// the the payload depending on its type. If it's an IPv4 datagram,
// the network interface passes it up the stack. If it's an ARP
// request or reply, the network interface processes the frame
// and learns or replies as necessary.
class NetworkInterface
{
private:
  // Ethernet (known as hardware, network-access, or link-layer) address of the interface
  EthernetAddress ethernet_address_;

  // IP (known as Internet-layer or network-layer) address of the interface
  Address ip_address_;

  std::unordered_map<uint32_t, std::pair<EthernetAddress, size_t>> ip2ether_ {};

  std::unordered_map<uint32_t, size_t> arp_timer_ {};

  std::unordered_map<size_t, std::vector<InternetDatagram>> waited_dgrams_ {};
  
  std::queue<EthernetFrame> out_frames_ {};

public:
  // Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer)
  // addresses
  NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address );

  // Access queue of Ethernet frames awaiting transmission
  std::optional<EthernetFrame> maybe_send();

  // Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination
  // address). Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address
  // for the next hop.
  // ("Sending" is accomplished by making sure maybe_send() will release the frame when next called,
  // but please consider the frame sent as soon as it is generated.)
  void send_datagram( const InternetDatagram& dgram, const Address& next_hop );

  // Receives an Ethernet frame and responds appropriately.
  // If type is IPv4, returns the datagram.
  // If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // If type is ARP reply, learn a mapping from the "sender" fields.
  std::optional<InternetDatagram> recv_frame( const EthernetFrame& frame );

  // Called periodically when time elapses
  void tick( size_t ms_since_last_tick );
};
```

- `network_interface.cc`

```c++
#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address ), ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  auto const& target_ip = next_hop.ipv4_numeric();
  if ( ip2ether_.contains( target_ip ) ) {
    EthernetFrame frame { { ip2ether_[target_ip].first, ethernet_address_, EthernetHeader::TYPE_IPv4 },
                          serialize( dgram ) };
    out_frames_.push( std::move( frame ) );
  } else {
    if ( !arp_timer_.contains( target_ip ) ) {
      ARPMessage request_msg;
      request_msg.opcode = ARPMessage::OPCODE_REQUEST;
      request_msg.sender_ethernet_address = ethernet_address_;
      request_msg.sender_ip_address = ip_address_.ipv4_numeric();
      request_msg.target_ip_address = target_ip;
      EthernetFrame frame { { ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP },
                            serialize( request_msg ) };
      out_frames_.push( std::move( frame ) );
      arp_timer_.emplace( target_ip, 0 );
      waited_dgrams_.insert( { target_ip, { dgram } } );
    } else {
      waited_dgrams_[target_ip].push_back( dgram );
    }
  }
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    return {};
  }
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      return dgram;
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage msg;
    if ( parse( msg, frame.payload ) ) {
      ip2ether_.insert( { msg.sender_ip_address, { msg.sender_ethernet_address, 0 } } );
      if ( msg.opcode == ARPMessage::OPCODE_REQUEST ) {
        if ( msg.target_ip_address == ip_address_.ipv4_numeric() ) {
          ARPMessage reply_msg;
          reply_msg.opcode = ARPMessage::OPCODE_REPLY;
          reply_msg.sender_ethernet_address = ethernet_address_;
          reply_msg.sender_ip_address = ip_address_.ipv4_numeric();
          reply_msg.target_ethernet_address = msg.sender_ethernet_address;
          reply_msg.target_ip_address = msg.sender_ip_address;
          EthernetFrame reply_frame { { msg.sender_ethernet_address, ethernet_address_, EthernetHeader::TYPE_ARP },
                                      serialize( reply_msg ) };
          out_frames_.push( std::move( reply_frame ) );
        }
      } else if ( msg.opcode == ARPMessage::OPCODE_REPLY ) {
        ip2ether_.insert( { msg.sender_ip_address, { msg.sender_ethernet_address, 0 } } );
        auto const& dgrams = waited_dgrams_[msg.sender_ip_address];
        for ( auto const& dgram : dgrams ) {
          send_datagram( dgram, Address::from_ipv4_numeric( msg.sender_ip_address ) );
        }
        waited_dgrams_.erase( msg.sender_ip_address );
      }
    }
  }
  return {};
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  static constexpr size_t IP_MAP_TTL = 30000;     // 30s
  static constexpr size_t ARP_REQUEST_TTL = 5000; // 5s

  for ( auto it = ip2ether_.begin(); it != ip2ether_.end(); ) {
    it->second.second += ms_since_last_tick;
    if ( it->second.second >= IP_MAP_TTL ) {
      it = ip2ether_.erase( it );
    } else {
      ++it;
    }
  }

  for ( auto it = arp_timer_.begin(); it != arp_timer_.end(); ) {
    it->second += ms_since_last_tick;
    if ( it->second >= ARP_REQUEST_TTL ) {
      it = arp_timer_.erase( it );
    } else {
      ++it;
    }
  }
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if ( out_frames_.empty() ) {
    return {};
  }
  auto frame = out_frames_.front();
  out_frames_.pop();
  return frame;
}
```

​	运行测试，结果如下：

<img src="assets/net_interface test result.png" alt="net_interface test result" style="zoom:67%;" />