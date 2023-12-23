# Lab Checkpoint 0: networking warmup

## 1. Set up GNU/Linux on your computer

CS144 的任务需要 GNU/Linux 操作系统和一个支持 C++ 2020 标准的 C++ 编译器（非常激进 ... ）

我的运行环境：**ubuntu-22.04.3** with **g++ 13.2.0**

## 2. Networking by hand

### 2.1 Fetch a Web page

**注意事项：**在与远程服务器建立 telnet 连接之后，需要稍微快速地输入请求内容。否则，外部主机将关闭连接。

```
telnet cs144.keithw.org http
GET /hello HTTP/1.1
Host: cs144.keithw.org 
Connection: close
# enter: Sends an empty line
```

获得以下输出，并且关闭连接。

```
HTTP/1.1 200 OK
Date: Tue, 19 Dec 2023 12:32:18 GMT
Server: Apache
Last-Modified: Thu, 13 Dec 2018 15:45:29 GMT
ETag: "e-57ce93446cb64"
Accept-Ranges: bytes
Content-Length: 14
Connection: close
Content-Type: text/plain

Hello, CS144!
Connection closed by foreign host.
```

> 在一些情况下，由于安全性问题，Telnet 并不被推荐作为远程登录的方式，因为它在传输过程中使用的是明文传输，存在安全风险。相比之下，SSH（Secure Shell）是一个更加安全的替代方案，它提供了加密的连接和更高的安全性。

### 2.2 Send yourself an email

由于没有 SUNet ID，这里跳过。

### 2.3 Listening and connecting

作为一个简单的服务器：等待客户端发起连接。

- `netcat` terminal window

  ```
  netcat -v -l -p 9090
  # netcat output here
  Listening on 0.0.0.0 9090
  Connection received on localhost 42352
  hello~
  ^C
  ```

- `telnet` terminal window

  ```
  telnet localhost 9090
  # telnet output here
  Trying 127.0.0.1...
  Connected to localhost.
  Escape character is '^]'.
  hello~
  Connection closed by foreign host.
  ```

## 3. Writing a network program using an OS stream socket

实现一个程序 `webget`，用于访问外部网页，类似于 wget。

**注意事项：**

- HTTP 头部的每一行末尾都是以 `\r\n` 结尾，而不是 `\n` ；
- 需要包含 `Connection: close` 的 HTTP 头部，或通过 `shutdown` 以指示远程服务器在处理完当前请求后直接关闭。
- 除非获取到 EOF，否则必须循环从远程服务器读取信息，因为网络数据的传输可能断断续续，需要多次 read。

```c++
void get_URL( const string& host, const string& path )
{
  TCPSocket sock{};
  sock.connect( Address( host, "http" ) );
  string message = "GET " + path + " HTTP/1.1\r\nHOST: " + host + "\r\n\r\n";
  sock.write(message);
  sock.shutdown(SHUT_WR);
  while( !sock.closed() && !sock.eof() ){
    string buffer;
    sock.read(buffer);
    cout << buffer;
  }
  sock.close();
}
```

运行测试，结果如下：

<img src="assets/check_webget result.png" alt="check_webget result" style="zoom: 67%;" />

## 4. An in-memory reliable byte stream

实现一个在**内存**中的有序可靠字节流（有点类似于管道）

> 后续实验会在**不可靠网络**中实现一个这样的可靠字节流，也就是**传输控制协议（Transmission Control Protocol，TCP）**

**注意事项：**

- 若要实现高性能的 ByteStream，需要使用**移动语义** `std::move(data)` 以**尽可能避免拷贝**
- 支持**流量控制**，以限制内存消耗，只有当缓冲区有空余时，Writer 才能继续写入。
- 必须考虑到字节流大于缓冲区大小的情况，即使缓冲区只有 $1$ 字节大小，所实现的程序也必须支持正常的写入读取操作。

整体代码：

- `byte_stream.hh` 代码

```c++
#pragma once

#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <stdint.h>

class Reader;
class Writer;

class ByteStream
{
protected:
  uint64_t capacity_;
  // Please add any additional state to the ByteStream here, and not to the Writer and Reader interfaces.
  bool is_closed_ { false };
  bool has_error_ { false };

  uint64_t num_bytes_pushed_ { 0 };
  uint64_t num_bytes_popped_ { 0 };
  uint64_t num_bytes_buffered_ { 0 };

  std::deque<std::string> data_queue_ {};
  std::deque<std::string_view> view_queue_ {};

public:
  explicit ByteStream( uint64_t capacity );

  // Helper functions (provided) to access the ByteStream's Reader and Writer interfaces
  Reader& reader();
  const Reader& reader() const;
  Writer& writer();
  const Writer& writer() const;
};

class Writer : public ByteStream
{
public:
  void push( std::string data ); // Push data to stream, but only as much as available capacity allows.

  void close();     // Signal that the stream has reached its ending. Nothing more will be written.
  void set_error(); // Signal that the stream suffered an error.

  bool is_closed() const;              // Has the stream been closed?
  uint64_t available_capacity() const; // How many bytes can be pushed to the stream right now?
  uint64_t bytes_pushed() const;       // Total number of bytes cumulatively pushed to the stream
};

class Reader : public ByteStream
{
public:
  std::string_view peek() const; // Peek at the next bytes in the buffer
  void pop( uint64_t len );      // Remove `len` bytes from the buffer

  bool is_finished() const; // Is the stream finished (closed and fully popped)?
  bool has_error() const;   // Has the stream had an error?

  uint64_t bytes_buffered() const; // Number of bytes currently buffered (pushed and not popped)
  uint64_t bytes_popped() const;   // Total number of bytes cumulatively popped from stream
};

/*
 * read: A (provided) helper function thats peeks and pops up to `len` bytes
 * from a ByteStream Reader into a string;
 */
void read( Reader& reader, uint64_t len, std::string& out );
```

- `byte_stream.cc` 代码

```c++
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
```

运行测试，结果如下：

<img src="assets/ByteStream test result.png" alt="ByteStream test result" style="zoom:67%;" />