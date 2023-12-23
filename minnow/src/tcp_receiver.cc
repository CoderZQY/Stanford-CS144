#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  if( !isn_.has_value() ){
    if( !message.SYN ) return;
    isn_ = message.seqno;
  }
  // 1. convert message.seqno to absolute seqno
  // checkpoint, i.e. first unassembled index (stream index)
  auto const checkpoint = inbound_stream.bytes_pushed() + 1; // + 1: stream index to absolute seqno
  auto const abs_seqno = message.seqno.unwrap( isn_.value(), checkpoint );

  // 2. convert absolute seqno to stream index
  auto const first_index = message.SYN ? 0 : abs_seqno - 1;
  reassembler.insert( first_index, message.payload.release(), message.FIN, inbound_stream);
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  TCPReceiverMessage msg {};
  auto const win_sz = inbound_stream.available_capacity();
  msg.window_size = win_sz < UINT16_MAX ? win_sz : UINT16_MAX;
  if ( isn_.has_value() ) {
    // convert from stream index to abs seqno
    // + 1 for SYN, + inbound_stream.is_closed() for FIN
    uint64_t const abs_seqno = inbound_stream.bytes_pushed() + 1 + inbound_stream.is_closed();
    msg.ackno = Wrap32::wrap( abs_seqno, isn_.value() );
  }
  return msg;
}
