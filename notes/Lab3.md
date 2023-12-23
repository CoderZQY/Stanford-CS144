# Lab Checkpoint 3: the TCP sender

## 2 Checkpoint 3: The TCP Sender

​	Lab3 将实现 TCP 中的 TCPSender，其需要完成的功能有：

- **跟踪 Receiver 的 TCPReceiverMessage 信息**（**ackno**s 和 **window size**s），通过将 ByteStream 的数据以 TCP Segments 的格式不断发送，**尽可能地填满 window**， 直到 window 满了或者 ByteStream 中没有任何东西可以发送。
- **跟踪那些已发送但还没有被接收的 segments**， 通常将这些数据被称为 "outstanding" segments。
- 若是这些 segments 在足够长的时间后没有没接收， 则**重传这些 segments 数据**。

​	这些功能实现了 "automatic repeat request (ARQ)" 机制，TCPSender 的任务就是确保 TCPReceiver 能收到每个字节至少一次。

### 2.1 How does the TCPSender know if a segment was lost?

​	TCPSender 会跟踪那些 outstanding segments 的状态，周期性调用 `tick` 函数以指示这些 segments 自发出以来所经过的时间。TCPSender 会在内部存储 TCPSenderMessages 的信息并遍历这个集合，找出那些已发出但经过时间太久但还未被接收的 segments 进行数据超时重传，直到所有的 seqno 都被接收。

- 那么如何界定**已发出但经过时间太久**这个定义就尤为重要，太长的等待时间会增加通信延时，而太短的等待时间则会浪费网络存储空间以及增大开销。需要注意如下几点：


1. `tick` 函数每隔几毫秒需要调用一次，记录自上一次调用以来经过的时间，并以此记录 TCPSender 存在的总时长。time 以及 clock 这类与操作系统和 CPU 相关的函数不允许调用，否则会导致 TCP 工作异常。
2. **retransmission timeout (RTO)** 在 TCPSender 创建时会进行初始化， 该值始终不变而 RTO 会不断改变（可能为初值的两倍，或者等于初值）以记录 outstanding segments 在超时重传前所经过的时间。
3. 包含数据的 segment 被传输后会启动重传定时器 **timer**，以 tick 为时间单位，直到经过 RTO 时间后会失效。
4. 当所有的 outstanding 数据都被接收后， 服务于重传机制的 timer 将会被停止。

- 若是 `tick` 被调用而 timer 已经失效了：

1. 重传最早的（seqno 最小的那个）未被接收的 segment。
2. 若此时 window size 非零：
   - 跟踪记录连续重传的数量，在每次重传后进行累加，TCPConnection 会使用这个信息判断 TCP 连接的可靠性，太多连续的重传意味着 TCP 连接不稳定需要终止。
   - 设置 RTO 值为原来的两倍，"exponential backoff" 以降低较差网络的重传速度，避免网络进一步拥堵。
3. 重置 timer 并启动，使其在 RTO 毫秒之后失效（需要考虑 RTO 已经翻倍）

- 
  当接收方给发送方发送了 ackno 表示成功接收了新的数据（这个 ackno 比之前任何一个 absolute seqno 都要大）：

1. 将 RTO 设置为其初始值。
2. 如果发送方仍有 outstanding 的数据，timer 需要重启并将会在 RTO 毫秒后失效。
3. 重置前述的连续重传数量为 0。

### 2.2 Implementing the TCP sender

​	需要实现以下 5 个接口并增添自己所需的变量以及 helper functions。

1. `void push( Reader& outbound_stream );`

   不断读取新的字节，并组装生成 TCPSenderMessage。需要使满足 window 尺寸的 TCPSenderMessage 尽可能大，但上限是 TCPConfig::MAX_PAYLOAD_SIZE (1452 bytes)。TCPSenderMessage::sequence_length() 会用来计算该 segment 占用的 seqno 的尺寸，SYN 和 FIN 都需要计算在内。

   **注意事项：**

   1. 需要注意 window size 最小应当为 $1$。

      > 仅适用于 push() 函数，当 window size 为 0 时，这时 TCPSender 仍需要发送一个独立的字节，虽然会被 TCPReceiver 拒绝接收但 TCPReceiver 会返回一个 TCPReceiverMessage 数据，这个信息可以告知 TCPSender window 中是否有新的用于传输的空间。否则 TCPSender 将无法确定何时发送新的 segment。
      >

   2. 需要两个布尔变量来记录是否需要给传输的 TCPSenderMessage 增加 SYN 标志或者 FIN 标志。此外，FIN 标志设定有一定的条件限制：

      - FIN 之前没设定过；
      - Reader 已经没有数据了；
      - 设置完 SYN 和 payload 之后的 window size 还要能容纳一个 FIN 的位置，如果本次 FIN 没有传输，就下次传输，不会有什么影响。

   3. 另外，只有在 outstanding segments 清空后我们才需要在传输新的 TCPSenderMessage 时重置 timer，否则 timer 将在很大程度上失去其基本作用。每次传输都需要更新 outstanding segments 集合以及发送的 seqno 数量，next_seqno 等信息。

2. `std::optional<TCPSenderMessage> maybe_send();`

   创建一个队列变量 queued_segments_ 来管理需要发送的 segments，使用队列可以保证传输的先后顺序。

3. `void receive( const TCPReceiverMessage& msg );`

   接收的 window 范围是 [ackno, ackno + window size]，TCPSender 需要遍历 outstanding_segments_ 集合将其中已经被 ACK 的部分移除（ackno 比 segments 中所有的 seqno 都要大的那些 segments）。

   **注意事项：**接收的 TCPReceiveMessage 中的 ackno 可能为空，这时候不能直接返回，有可能是我们传输了一个 empty message 去获取 window size 而返回的一个包。因此，我们仅需要更新 window_size_ 变量即可。

4. `void tick( const size_t ms_since_last_tick );` 计时单位，得到与上次该函数被调用的时间间隔。

   除了要创建一个 timer_ 变量存储时间外，在 tick 中还需要做 2.1 TCPSender 如何监测丢包中的几件事：

   - 若 outstanding segments 存在且 timer 失效，重传时间最早的那个 segments。
   - window size 非零时累加连续重传数量，并进行 "exponential backoff"。
   - 重置 timer 的值为 0，为下一次重传准备。

5. `void send_empty_message();` 发送长度为零并且 seqno 正确的 TCPSenderMessage，在 TCPReceiver 希望通过 TCPReceiverMessage 获取一些特定信息的时候特别有用，这个和 push 中的那个想法很类似。

   **注意事项：**这种长度为 0 的 segments 不需要被监测并纳入 outstanding segments 用以重传。只需要返回一个仅有 ackno 被赋值为下一个需要接收的字节的 seqno 的 TCPSenderMessage。

​	整体代码如下：

- `tcp_sender.hh`

```c++
#pragma once
#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

class Timer{
private:
  uint64_t initial_RTO_ms_;
  uint64_t curr_RTO_ms;
  size_t time_ms_ { 0 };
  bool running_ { false };

public:
  explicit Timer( uint64_t init_RTO ) : initial_RTO_ms_( init_RTO ), curr_RTO_ms( init_RTO ) {}
  void start()
  {
    running_ = true;
    time_ms_ = 0;
  }
  void stop() { running_ = false; }

  bool is_running() const { return running_; }

  bool is_expired() const { return running_ && ( time_ms_ >= curr_RTO_ms ); }

  void tick( size_t const ms_since_last_tick )
  {
    if ( running_ ) {
      time_ms_ += ms_since_last_tick;
    }
  }

  void double_RTO() { curr_RTO_ms *= 2; }

  void reset_RTO() { curr_RTO_ms = initial_RTO_ms_; }
};

class TCPSender
{
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  
  bool syn_ { false };
  bool fin_ { false };
  unsigned retransmit_cnt_ { 0 }; // consecutive re-transmit count

  uint64_t acked_seqno_ { 0 };
  uint64_t next_seqno_ { 0 };
  uint16_t window_size_ { 1 };
  
  uint64_t outstanding_cnt_ { 0 }; // sequence_numbers_in_flight
  
  std::queue<TCPSenderMessage> outstanding_segments_ {};
  std::queue<TCPSenderMessage> queued_segments_ {};

  Timer timer_ { initial_RTO_ms_ };

public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn );

  /* Push bytes from the outbound stream */
  void push( Reader& outbound_stream );

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const;

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called. */
  void tick( uint64_t ms_since_last_tick );

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
};
```

- `tcp_sender.cc`

```c++
#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <random>

using namespace std;

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender( uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn )
  : isn_( fixed_isn.value_or( Wrap32 { random_device()() } ) ), initial_RTO_ms_( initial_RTO_ms )
{}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return outstanding_cnt_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmit_cnt_;
}

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if ( queued_segments_.empty() ) {
    return {};
  }
  if ( !timer_.is_running() ) {
    timer_.start();
  }
  auto msg = queued_segments_.front();
  queued_segments_.pop();
  return msg;
}

void TCPSender::push( Reader& outbound_stream )
{
  size_t curr_window_size = window_size_ != 0 ? window_size_ : 1; // Special case for a zero-size window.
  while ( outstanding_cnt_ < curr_window_size ) {
    TCPSenderMessage msg; // seqno, SYN, payload, FIN
    if ( !syn_ ) {
      syn_ = msg.SYN = true;
      outstanding_cnt_ += 1;
    }
    msg.seqno = Wrap32::wrap( next_seqno_, isn_ );
    auto const payload_size = min( TCPConfig::MAX_PAYLOAD_SIZE, curr_window_size - outstanding_cnt_ );
    read( outbound_stream, payload_size, msg.payload );
    outstanding_cnt_ += msg.payload.size();
    if ( !fin_ && outbound_stream.is_finished() && outstanding_cnt_ < curr_window_size ) {
      fin_ = msg.FIN = true;
      outstanding_cnt_ += 1;
    }
    if ( msg.sequence_length() == 0 ) {
      break;
    }
    queued_segments_.push( msg );
    next_seqno_ += msg.sequence_length();
    outstanding_segments_.push( msg );
    if ( msg.FIN || outbound_stream.bytes_buffered() == 0 ) {
      break;
    }
  }
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  auto seqno = Wrap32::wrap( next_seqno_, isn_ );
  return { seqno, false, {}, false };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  window_size_ = msg.window_size;
  if ( msg.ackno.has_value() ) {
    auto ackno = msg.ackno.value().unwrap( isn_, next_seqno_ );
    if ( ackno > next_seqno_ ) {
      return;
    }
    acked_seqno_ = ackno;
    while ( !outstanding_segments_.empty() ) {
      auto& front_msg = outstanding_segments_.front();
      if ( front_msg.seqno.unwrap( isn_, next_seqno_ ) + front_msg.sequence_length() <= acked_seqno_ ) {
        outstanding_cnt_ -= front_msg.sequence_length();
        outstanding_segments_.pop();
        timer_.reset_RTO();
        if ( !outstanding_segments_.empty() ) {
          timer_.start();
        }
        retransmit_cnt_ = 0;
      } else {
        break;
      }
    }
    if ( outstanding_segments_.empty() ) {
      timer_.stop();
    }
  }
}

void TCPSender::tick( const size_t ms_since_last_tick )
{
  timer_.tick( ms_since_last_tick );
  if ( timer_.is_expired() ) {
    queued_segments_.push( outstanding_segments_.front() );
    if ( window_size_ != 0 ) {
      ++retransmit_cnt_;
      timer_.double_RTO();
    }
    timer_.start();
  }
}
```

​	运行测试，结果如下：

<img src="assets/TCP sender test result.png" alt="TCP sender test result" style="zoom:60%;" />