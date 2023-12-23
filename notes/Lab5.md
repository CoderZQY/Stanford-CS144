# Lab Checkpoint 5: building an IP router

## 1. Overview

<img src="assets/IP router structure.png" alt="IP router structure" style="zoom:60%;" />

​	Lab5 要求实现一个简易的路由器，通常路由器会有多个网络接口，能够从任意一个接口接收网络数据报。路由器的作用就是将 datagrams 依据路由表进行转发，路由表定义了路由转发的一些规则：

1. 确定转发的接口；
2. 确定下一跳的 IP 地址；

​	在 Lab5 中需要实现一个新的 Router 类，**跟踪路由表信息**并且将接收到的每个 datagram 通过正确的输出端 NetworkInterface **正确转发**到下一跳。后续需要通过 IP 的**最长前缀匹配**来实现路由的功能，这也是实验中最为棘手的部分。

## 3. Implementing the Router

1. `void add_route(uint32_t route_prefix, uint8_t prefix_length, optional<Address> next_hop, size_t interface_num);` 调用该方法将路由信息添加到路由表中，需要自己添加存储相关信息的数据结构。
2. `void route();` 该方法需要对输入的 datagrams 进行路由，将其通过正确的网络接口转发到下一跳，这需要实现 "最长前缀匹配 （longest-prefix match）" 以找到最合适的路由方案，该方法有如下细节需要实现：
   - 路由器需要搜索路由表找到匹配 datagrams 中的目的地址的那个路由。这意味着目的地址的 prefix_length bits 需要与 route_prefix 的 prefix_length bits 完全一致。
   - 在匹配的路由方案中路由器选择 prefix_length 值最大的那个，即 "最长前缀匹配" 的路由。
   - 如果没有匹配的路由信息，则丢弃该 datagram。
   - 每一跳都需要减少 datagrams 的 TTL (time to live)。如果 TTL 已经归零或者在本次减一后到零，路由器同样需要丢弃该项 datagram。
   - 最终，路由器需要将修改过的 datagram 通过合适的网络接口（`interface(interface_num).send_datagram()`）发送到下一跳。

​	**注意事项：**在修改了 dgram.header.ttl 后一定要重新计算 checksum，否则执行测试案例解析 InternetDatagram 的时候会出错。

## 5. Simulated router and interfaces topology

<img src="assets/Simulated router and interfaces topology.png" alt="Simulated router and interfaces topology" style="zoom:50%;" />

​	完整代码如下：

- `router.hh`

```c++
#pragma once

#include "network_interface.hh"

#include <optional>
#include <queue>

// A wrapper for NetworkInterface that makes the host-side
// interface asynchronous: instead of returning received datagrams
// immediately (from the `recv_frame` method), it stores them for
// later retrieval. Otherwise, behaves identically to the underlying
// implementation of NetworkInterface.
class AsyncNetworkInterface : public NetworkInterface
{
  std::queue<InternetDatagram> datagrams_in_ {};

public:
  using NetworkInterface::NetworkInterface;

  // Construct from a NetworkInterface
  explicit AsyncNetworkInterface( NetworkInterface&& interface ) : NetworkInterface( interface ) {}

  // \brief Receives and Ethernet frame and responds appropriately.

  // - If type is IPv4, pushes to the `datagrams_out` queue for later retrieval by the owner.
  // - If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // - If type is ARP reply, learn a mapping from the "target" fields.
  //
  // \param[in] frame the incoming Ethernet frame
  void recv_frame( const EthernetFrame& frame )
  {
    auto optional_dgram = NetworkInterface::recv_frame( frame );
    if ( optional_dgram.has_value() ) {
      datagrams_in_.push( std::move( optional_dgram.value() ) );
    }
  };

  // Access queue of Internet datagrams that have been received
  std::optional<InternetDatagram> maybe_receive()
  {
    if ( datagrams_in_.empty() ) {
      return {};
    }

    InternetDatagram datagram = std::move( datagrams_in_.front() );
    datagrams_in_.pop();
    return datagram;
  }
};

// A router that has multiple network interfaces and
// performs longest-prefix-match routing between them.
class Router
{
  struct Item
  {
    uint32_t route_prefix {};
    uint8_t prefix_length {};
    std::optional<Address> next_hop;
    size_t interface_num {};
  };

  // The router's collection of network interfaces
  std::vector<AsyncNetworkInterface> interfaces_ {};

  std::vector<Item> routing_table_ {};

  std::vector<Item>::iterator longest_prefix_match_( uint32_t dst_ip );
  
  static int match_length_( uint32_t src_ip, uint32_t tgt_ip, uint8_t tgt_len );
public:
  // Add an interface to the router
  // interface: an already-constructed network interface
  // returns the index of the interface after it has been added to the router
  size_t add_interface( AsyncNetworkInterface&& interface )
  {
    interfaces_.push_back( std::move( interface ) );
    return interfaces_.size() - 1;
  }

  // Access an interface by index
  AsyncNetworkInterface& interface( size_t N ) { return interfaces_.at( N ); }

  // Add a route (a forwarding rule)
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  // Route packets between the interfaces. For each interface, use the
  // maybe_receive() method to consume every incoming datagram and
  // send it on one of interfaces to the correct next hop. The router
  // chooses the outbound interface and next-hop as specified by the
  // route with the longest prefix_length that matches the datagram's
  // destination address.
  void route();
};
```

- `router.cc`

```c++
#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  routing_table_.emplace_back( route_prefix, prefix_length, next_hop, interface_num );
}

void Router::route() {
  for ( auto& current_interface : interfaces_ ) {
    auto received_dgram = current_interface.maybe_receive();
    if ( received_dgram.has_value() ) {
      auto& dgram = received_dgram.value();
      if ( dgram.header.ttl > 1 ) {
        dgram.header.ttl--;
        // NOTE: important!!!
        dgram.header.compute_checksum();
        auto dst_ip = dgram.header.dst;
        auto it = longest_prefix_match_( dst_ip );
        if ( it != routing_table_.end() ) {
          auto& target_interface = interface( it->interface_num );
          target_interface.send_datagram( dgram, it->next_hop.value_or( Address::from_ipv4_numeric( dst_ip ) ) );
        }
      }
    }
  }
}

std::vector<Router::Item>::iterator Router::longest_prefix_match_( uint32_t dst_ip )
{
  auto res = routing_table_.end();
  auto max_length = -1;
  for ( auto it = routing_table_.begin(); it != routing_table_.end(); ++it ) {
    auto len = match_length_( dst_ip, it->route_prefix, it->prefix_length );
    if ( len > max_length ) {
      max_length = len;
      res = it;
    }
  }
  return res;
}

int Router::match_length_( uint32_t src_ip, uint32_t tgt_ip, uint8_t tgt_len )
{
  if ( tgt_len == 0 ) {
    return 0;
  }

  if ( tgt_len > 32 ) {
    return -1;
  }

  // tgt_len < 32
  uint8_t const len = 32U - tgt_len;
  src_ip = src_ip >> len;
  tgt_ip = tgt_ip >> len;
  return src_ip == tgt_ip ? tgt_len : -1;
}
```

​	运行测试，结果如下：

<img src="assets/router test result.png" alt="router test result" style="zoom:60%;" />