# Lab Checkpoint 1: stitching substrings into a byte stream

## 0. Overview

<img src="assets/CS144 Labs'structure 1-3.png" alt="CS144 Labs'structure 1-3" style="zoom:50%;" />

​	由于底层网络是以 "best effort" 的形式在进行数据报 (datagram) 的传输，意味着在数据包传输的过程中，很可能会发生**丢失**、 **重复**、**乱序**或者**被替换**的现象，因此需要 TCP 为数据包提供可靠的底层逻辑。Lab1 的实验需要完成 ByteStream 外部的 **StreamReassembler** 部分。

## 2. Putting substrings in sequence

​	需要实现的接口如下：

```c++
// Insert a new substring to be reassembled into a ByteStream.
void insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output );

// How many bytes are stored in the Reassembler itself?
uint64_t bytes_pending() const;
```

### 2.1 What should the Reassembler store internally?

​	**Reassembler** 需要处理以下情况：

1. 收到的字节流正好是 ByteStream 所需的下一组字节，直接用 Writer 写入到 ByteStream 的缓存中。
2. 若 ByteStream 已经存满，并且此时有部分字节完成了 Reassembler 的处理需要传入 ByteStream，则需要在 Reassembler 内部的缓存空间进行缓存。
3. 当收到的字节流符合当前的剩余空间，但其更早的字节却不存在的时候，需要将其缓存在 Reassembler 中。
4. 丢弃超出剩余空间的字节。


​	**注意事项**：重点是第三点：**如何保存失序的字节**。由于不能包含重叠数据，并且还需要及时 push 进去，因此需要一个数据结构，唯一的保存字节和对应的索引。


​	指导书中强调无论子串如何到达，需要限制 Reassembler 和 ByteStream 对内存的消耗，并给出了下图进行说明：

<img src="assets/Reassembler store space.png" alt="Reassembler store space" style="zoom:50%;" />

- `first_unpopped_index`： 已经排序整理且验证正确的部分的起始索引，绿色区块存储在 ByteStream 的缓存中等待发送。

- `first_unassembled_index`： 尚未排序整理的待验证的部分， 可称其为子列 （substrings）的起始索引，红色区块存储在 Reassembler 的内部缓存区域。

- `first_unacceptable_index`： 需要被丢弃的部分的起始索引。




整体代码：

- `reassembler.hh` 代码

```c++
#pragma once

#include "byte_stream.hh"

#include <string>

#include <list>

class Reassembler
{
private:
  uint64_t first_unassembled_index_ { 0 };

  std::list<std::pair<uint64_t, std::string>> buffer_ {};
    
  uint64_t buffer_size_ { 0 };
    
  bool has_last_ { false };

  // insert valid but un-ordered data into buffer
  void insert_into_buffer(  uint64_t first_index, std::string&& data,  bool is_last_substring );

  // pop invalid bytes and insert valid bytes into writer
  void pop_from_buffer( Writer& output );
public:
  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;
};
```

- `reassembler.cc` 代码

```c++
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
```

运行测试，结果如下：

<img src="assets/StreamReassembler test result.png" alt="StreamReassembler test result" style="zoom:67%;" />