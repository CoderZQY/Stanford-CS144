#include "reassembler.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output )
{
  if ( data.empty() ) {
    if ( is_last_substring ) {
      output.close();
    }
    return;
  }

  if ( output.available_capacity() == 0 ) {
    return;
  }

  auto const end_index = first_index + data.size();
  auto const first_unacceptable = first_unassembled_index_ + output.available_capacity();

  // data is not in [first_unassembled_index, first_unacceptable)
  if ( end_index <= first_unassembled_index_ || first_index >= first_unacceptable ) {
    return;
  }

  // if part of data is out of capacity, then truncate it
  if ( end_index > first_unacceptable ) {
    data = data.substr( 0, first_unacceptable - first_index );
    // if truncated, it won't be last_substring
    is_last_substring = false;
  }

  // unordered bytes, save it in buffer and return
  if ( first_index > first_unassembled_index_ ) {
    insert_into_buffer( first_index, std::move( data ), is_last_substring );
    return;
  }

  // remove useless prefix of data (i.e. bytes which are already assembled)
  if ( first_index < first_unassembled_index_ ) {
    data = data.substr( first_unassembled_index_ - first_index );
  }

  // here we have first_index == first_unassembled_index_
  first_unassembled_index_ += data.size();
  output.push( std::move( data ) );

  if ( is_last_substring ) {
    output.close();
  }

  if ( !buffer_.empty() && buffer_.begin()->first <= first_unassembled_index_ ) {
    pop_from_buffer( output );
  }
}

uint64_t Reassembler::bytes_pending() const
{
  return buffer_size_;
}

void Reassembler::insert_into_buffer( const uint64_t first_index, std::string&& data, const bool is_last_substring )
{
  auto begin_index = first_index;
  const auto end_index = first_index + data.size();

  for ( auto it = buffer_.begin(); it != buffer_.end() && begin_index < end_index; ) {
    if ( it->first <= begin_index ) {
      begin_index = max( begin_index, it->first + it->second.size() );
      ++it;
      continue;
    }

    if ( begin_index == first_index && end_index <= it->first ) {
      buffer_size_ += data.size();
      buffer_.emplace( it, first_index, std::move( data ) );
      return;
    }

    const auto right_index = min( it->first, end_index );
    const auto len = right_index - begin_index;
    buffer_.emplace( it, begin_index, data.substr( begin_index - first_index, len ) );
    buffer_size_ += len;
    begin_index = right_index;
  }

  if ( begin_index < end_index ) {
    buffer_size_ += end_index - begin_index;
    buffer_.emplace_back( begin_index, data.substr( begin_index - first_index ) );
  }

  if ( is_last_substring ) {
    has_last_ = true;
  }
}

void Reassembler::pop_from_buffer( Writer& output )
{
  for ( auto it = buffer_.begin(); it != buffer_.end(); ) {
    if ( it->first > first_unassembled_index_ ) {
      break;
    }
    // it->first <= first_unassembled_index_
    const auto end = it->first + it->second.size();
    if ( end <= first_unassembled_index_ ) {
      buffer_size_ -= it->second.size();
    } else {
      auto data = std::move( it->second );
      buffer_size_ -= data.size();
      data = data.substr( first_unassembled_index_ - it->first );
      first_unassembled_index_ += data.size();
      output.push( std::move( data ) );
    }
    it = buffer_.erase( it );
  }

  if ( buffer_.empty() && has_last_ ) {
    output.close();
  }
}