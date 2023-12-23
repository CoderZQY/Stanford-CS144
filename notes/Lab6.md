# Checkpoint 6: putting it all together

## 1. Overview

Lab6 是一个选择性的实验，在前 6 个实验中我们已经完成了网络的框架：

1. Checkpoint 0 (a reliable byte stream)；
2. Checkpoints 1–3 (the Transmission Control Protocol)；
3. Checkpoint 4 (an IP/Ethernet network interface)；
4. Checkpoint 5 (an IP router)， 

该实验的目的是使用前述的网络栈实现真实的网络通信，这需要我们有个 partner， 或者一人分饰两角。

## 3. The Network

该实验需要一对可行的网络栈组合成一个真实的网络通信环境，两方都需要构建 host 和 router，网络拓扑如下图所示：

<img src="assets/The Network.png" alt="The Network" style="zoom:60%;" />

1. 对于个人而言，需要使用同一套代码在不同的终端中启用 server 以及 client；

2. 为了使用 relay，需要选择任意的一个 1024 到 64000 之间的偶数以区分不同的 group；

3. server 端在 build 文件夹下输入 ./apps/endtoend server cs144.keithw.org 2048，若顺利的话 server 端会打印如下信息：

   <img src="assets/check6_server 端.png" alt="check6_server 端" style="zoom:67%;" />

4. client 端在 build 文件夹下输入 ./apps/endtoend client cs144.keithw.org 2048， 若顺利的话 client 端会打印如下信息：

   <img src="assets/check6_client 端.png" alt="check6_client 端" style="zoom:67%;" />

   并且 server 端会额外打印一条信息：New connection from 192.168.0.50:55785.

5. 若以上信息都能正确打印，则意味着 TCP 握手成功，若有问题则需要在之前的命令后加上 debug 进入 debug 模式进行调试，这将输出所有交换的以太网帧（ARP 和 TCP/IP 帧）

   - 我们可以在 server 或者 client 任意一端输入数据，就能在相应的另一端看到数据。

     > 实验发现需要在 Terminal 输入需要发送的数据后按下 Ctrl-D 才能发送数据并在 remote 端看到。
     >

   - 可以使用 Ctrl-D 关闭链接。server 或 client 关闭链接后会终止那一端的 ByteStream 的输出，但仍会持续接收数据直到另一端也关闭了 ByteStream 的输出，我们需要验证这点。

     > 在确保 Terminal 中已经没有数据后，此时按下 Ctrl-D 才会断开 TCP 的链接。
     >

## 4. Sending a file

1. 将 1MB 大小的随机数写入文件 /tmp/big.txt。

   ```
   dd if=/dev/urandom bs=1M count=1 of=/tmp/big.txt
   ```

2. 让 server 端在接受连接后立刻发送文件。

   ```
   ./apps/endtoend server cs144.keithw.org even_number < /tmp/big.txt
   ```

3. 让 client 端关闭输出数据流并下载文件。

   ```
   </dev/null ./apps/endtoend client cs144.keithw.org odd_number > /tmp/big-received.txt
   ```

4. 比较两个文件确保二者一致

   ```
   sha256sum /tmp/big.txt 或者 sha256sum /tmp/big-received.txt
   ```

​	测试结果：

- server 端

<img src="assets/server_sending a file.png" alt="server_sending a file" style="zoom:60%;" />

- client 端

<img src="assets/client_sending a file.png" alt="client_sending a file" style="zoom:60%;" />

- 比较 SHA-256 散列值

  > SHA-256 是一种加密散列函数，用于产生给定输入数据的固定长度（256 位）的哈希值。这种哈希值通常用于验证文件的完整性或检查文件是否在传输过程中被篡改。

<img src="assets/SHA-256 哈希值对比.png" alt="SHA-256 哈希值对比" style="zoom:67%;" />