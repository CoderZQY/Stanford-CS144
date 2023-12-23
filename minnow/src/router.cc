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