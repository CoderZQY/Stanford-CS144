#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  if ( available_capacity() == 0 || data.empty() ) {
    return;
  }
  auto const n = min( available_capacity(), data.size() );
  if ( n < data.size() ) {
    data = data.substr( 0, n );
  }
  data_queue_.push_back( std::move( data ) );
  view_queue_.emplace_back( data_queue_.back().c_str(), n);
  num_bytes_buffered_ += n;
  num_bytes_pushed_ += n;
}

void Writer::close()
{
  is_closed_ = true;
}

void Writer::set_error()
{
  has_error_ = true;
}

bool Writer::is_closed() const
{
  return is_closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - num_bytes_buffered_;
}

uint64_t Writer::bytes_pushed() const
{
  return num_bytes_pushed_;
}

string_view Reader::peek() const
{
  if ( view_queue_.empty() ) {
    return {};
  }
  return view_queue_.front();
}

bool Reader::is_finished() const
{
  return is_closed_ && num_bytes_buffered_ == 0;
}

bool Reader::has_error() const
{
  return has_error_;
}

void Reader::pop( uint64_t len )
{
  auto n = min( len, num_bytes_buffered_ );
  while ( n > 0 ) {
    auto sz = view_queue_.front().size();
    if ( n < sz ) {
      view_queue_.front().remove_prefix( n );
      num_bytes_buffered_ -= n;
      num_bytes_popped_ += n;
      return;
    }
    view_queue_.pop_front();
    data_queue_.pop_front();
    n -= sz;
    num_bytes_buffered_ -= sz;
    num_bytes_popped_ += sz;
  }
}

uint64_t Reader::bytes_buffered() const
{
  return num_bytes_buffered_;
}

uint64_t Reader::bytes_popped() const
{
  return num_bytes_popped_;
}