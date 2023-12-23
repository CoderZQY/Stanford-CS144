# Lab Checkpoint 2: the TCP receiver

​	Lab2 将实现 TCP 协议的 TCPReceiver，通过 `receive()` 函数从 peer 端接收数据， 经过 Reassembler 处理写入 ByteStream 缓存， 应用层就可以通过 TCPSocket 读取数据。

​	在接收 peer 端数据的同时， TCPReceiver 通过 `send()` 函数承担着告知 peer 一些重要信息的职责， 这些信息包括这两个部分：

- **Acknowledgment**: `first_unassembled_index` 又称 **`ackno`**， 是 TCPReceiver 从 peer 发送端希望收到的 第一个字节的索引号。
- **Flow Control**: **`window size`**， 是 ByteStream 可以写入的剩余空间， 限制了 TCPSender 发送数据的 index 的实际范围。 通过这个 window， TCPReceiver 能够 对输入的数据流量进行控制， 限制发送端的数据直到接收端准备好继续接收。

> 通常将 ackno 称作 window 的索引左边界 （Smallest Index）， 将 ackno + window size 称作 window 的索引右边界 （Largest Index）
>

## 2 Checkpoint 2: The TCP Receiver

### 2.1 Translating between 64-bit indexes and 32-bit seqnos

​	Reassembler 重组 substrings 时每一个字节的索引号都是 64-bit， 并且其首位 index 始终是从零开始。 这是一组在当前技术条件下永远不可能溢出的数据流索引号组。 但是在 TCP segment 的头部数据中，sequence number (seqno) 是 32-bit 的， 这组索引号与前述的数据流索引号在长度上不匹配，这种不匹配引入了一些复杂性：

1. TCP 的数据流可以任意长，但 TCP 报文段的头部 seqno 仅有 32-bit，4 GB大小，当 seqno **达到 $2^{32}-1$ 后，会被置零**；
2. 为提高鲁棒性和避免数据混淆，TCP seqno 以一个 32-bit 的随机值作为起始，被称作：**ISN (Initial Sequence Number)**，代表了当前数据流的 "zero point"，或称其为 SYN (beginning of stream)；
3. 有 SYN (beginning of stream) 自然也有 FIN (end of stream)， 它们不属于数据流中的任意字节，仅作为数据流的起始和末尾的两个标识符存在，但 **SYN 和 FIN 分别占一个 seqno**；

> Transmitting at 100 gigabits/sec, it would take almost 50 years to reach $2^{64}$ bytes. By contrast, it takes only a third of a second to reach $2^{32}$ bytes.

​	CS144 区分了三种类型的索引值：`seqno`，`absolute seqno`， 以及 `stream index`，并给出了一个 "cat" 数据的例子。这些索引需要我们实现其相互之间的转换，并保持转换前后不同数据的这三类索引值之间的关系一致。

<img src="assets/cat example.png" alt="cat example" style="zoom:67%;" />

<img src="assets/three different types of indexing.png" alt="three different types of indexing" style="zoom:60%;" />

- **wrap**：将 `absolute seqno` $\rightarrow$ `seqno`

  ```c++
  static Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
  ```

  这个的实现很简单，只需要 zero_point + n 再转换成 Wrap32 即可。

  ```c++
  Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
  {
    // n implicitly convert to uint32_t in order to match the signature of operator+
    return zero_point + n;
  }
  ```

- **unwrap**：将 `seqno` $\rightarrow$ `absolute seqno`，这个要麻烦一些。

  ```c++
  uint64_t unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
  ```

  关于 `checkpoint` 的作用：E.g. 如果 ISN 为 $0$，seqno $17$ 可能对应着多个 absolute seqno: $17$、$2^{32}+17$、$2^{33}+17$、$2^{33}+2^{32}+17$、$2^{34}+17$、$2^{34}+2^{32}+17$ 等等。文档指出："The wrap/unwrap operations should **preserve offsets**—two seqnos that differ by 17 will correspond to two absolute seqnos that also differ by 17." `checkpoint` 能帮助避免歧义，这里实际上就是 first_unassembled_index。

  因此可以将 **checkpoint**（**64位**）利用上面的 wrap 函数转换为 **ckpt**（**32位**），计算 raw_value 和 ckpt 之间的偏移量 offset，再加到 checkpoint 上。这样最终的结果可以大概写成这样：`return checkpoint + (raw_value - ckpt)` 。

  而对于任意一个 $32$ 位的 seqno，有两个点最靠近 `checkpoint`，一个在左边，一个在右边。在 $32$ 位空间中，从一个点到另一个点，既可以向右移动 $X$ 距离，也可以向左移动 $2^{32}-X$，对应到 $64$ 位空间中，就是 `checkpoint` 右边：`checkpoint + X`，左边：`checkpoint - (2^32 - X)`，因此只需要判断 $X$ 和 $2^{32}-X$ 哪个更小即可。还有一种特殊情况，即如果 `checkpoint` 不够左移（`checkpoint < 2^32 - X`），那么就只能选右边的点。

  ```c++
  uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
  {
    static constexpr uint64_t TWO31 = 1UL << 31;
    static constexpr uint64_t TWO32 = 1UL << 32;
  
    auto const ckpt32 = wrap( checkpoint, zero_point );
    uint64_t const dis = raw_value_ - ckpt32.raw_value_;
  
    if ( dis <= TWO31 || checkpoint + dis < TWO32 ) {
      return checkpoint + dis;
    }
    return checkpoint + dis - TWO32;
  }
  ```

### 2.2 Implementing the TCP receiver

CS144 提供了 `TCPSenderMessage` 和 `TCPReceiverMessage`，前者用于 receive 后者用于 send。

```c++
struct TCPSenderMessage
{
  Wrap32 seqno { 0 };
  bool SYN { false };
  Buffer payload {};
  bool FIN { false };
  // How many sequence numbers does this segment use?
  size_t sequence_length() const { return SYN + payload.size() + FIN; }
};
```

```c++
struct TCPReceiverMessage
{
  std::optional<Wrap32> ackno {};
  uint16_t window_size {};
};
```

#### 2.2.1 receive()

- 第一个到达的 TCP 报文段中包含 SYN 标志，需要保存其中的 seqno 作为 ISN；
- 将 payload 部分的数据推送给 Reassembler，FIN 作为标识符控制 push 过程的终止；

​	此外，传输的时候使用的是 stream index，而我们收到的是 seqno，因此首先需要将 **seqno 转为 absolute seqno**，我们已经写好了 unwrap 函数，需要提供 first_unassembled_index 作为 checkpoint，也就是下一个需要被 buffer 存储的字节。在 Lab0 中实现的 Writer 类有一个 bytes_pushed() 函数，`bytes_pushed() + 1 = checkpoint`。

​	**absolute seqno 转换为 stream index** 时需要考虑当前的 message 是否有 SYN 部分。 给 unwrap 提供的 zero_point 是以 SYN 对应的 seqno，若 SYN 在当前的 message 中不存在， 则 zero_point 应当为当前 payload 的第一个字节。 所以有如下转换满足：`uint64_t stream_index = abs_seqno - 1 + message.SYN`，之后只需要提供 inbound_stream.insert() 所需要的各个变量即可。

​	在 tcp_receiver.hh 中添加一个 `std::optional<Wrap32> isn_ {};`

```c++
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
```

#### 2.2.2 send()

- 只有 ackno 是 optional 属性，window_size 是每次都要发送的；

- 通过 inbound_stream.is_closed() 判断当前是否收到 FIN，同时别忘了传输开始后，SYN 也占一个 seqno 位置。

```c++
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
```

运行测试，结果如下：

<img src="assets/TCP receiver test result.png" alt="TCP receiver test result" style="zoom:60%;" />