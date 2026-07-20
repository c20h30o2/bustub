
› 没有看懂网站上的描述


• 我把 Project 1 的官网要求翻成“要实现什么、各组件怎么配合、推荐顺序”，先不陷入接口细节。

• Searching the web

• Searched the web

─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

• 先纠正一下：你这个 Fall 2025 版本使用的是 ARC Replacer，不是旧版 Project 1 的 LRU-K。

  Project 1 本质上是实现一个“数据库页面缓存”。

  磁盘中的 Page
      ↕ DiskScheduler：负责异步磁盘读写
  内存中的 Frame
      ↕ BufferPoolManager：管理 Page 与 Frame 的映射
  数据库其他组件

  核心概念：

  - Page：数据库的一个逻辑数据页，大小 8 KB，有唯一 page_id。
  - Frame：内存中一块 8 KB 空间，用来临时装一个 Page。
  - Buffer Pool：固定数量的 Frame。比如只有 10 个 Frame，却可以访问磁盘上成千上万个 Page。
  - pin_count：正在使用某个 Page 的线程数。大于 0 时不能驱逐。
  - is_dirty：内存中的 Page 是否被修改。驱逐脏页前必须写回磁盘。

  需要完成三个主要组件：

  1. ArcReplacer

  当内存 Frame 用完时，决定驱逐哪个可驱逐 Frame。ARC 同时考虑“最近访问”和“频繁访问”。

  2. DiskScheduler

  维护磁盘请求队列和后台线程，把读写请求交给 DiskManager，再通过 promise 通知调用者完成。

  3. BufferPoolManager

  把前两个组件组装起来：

  - 页面已在内存：找到对应 Frame，增加 pin_count。
  - 页面不在内存：取得空闲 Frame，或让 ARC 驱逐一个 Frame，再从磁盘读取。
  - 驱逐脏页：先写回磁盘。
  - 页面不再使用：降低 pin_count，降到 0 后允许 ARC 驱逐。

  还需要实现 ReadPageGuard 和 WritePageGuard。它们是 RAII 对象：获得 Guard 时锁住并 pin 页面，Guard 销毁或 Drop() 时自动解锁和 unpin。

  推荐顺序：

  ArcReplacer
  → DiskScheduler
  → PageGuard 基础移动语义
  → BufferPoolManager
  → PageGuard 与 BufferPoolManager 联调

  第一步只需要关注 src/include/buffer/arc_replacer.h 和 src/buffer/arc_replacer.cpp。官网完整要求见 Fall 2025 Project #1 (https://15445.courses.cs.cmu.edu/fall2025/project1/)。

─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────


› 没有看懂arc相关的描述


• ARC 可以先理解成：它同时防止两类页面被错误淘汰：

  - 最近刚用过一次的页面
  - 已经反复使用的热门页面

  它维护四个队列：

   队列         页面是否还在内存    含义
  ━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━
   MRU                        是    最近只访问过一次
  ───────────  ──────────────────  ───────────────────────────
   MFU                        是    已访问过多次
  ───────────  ──────────────────  ───────────────────────────
   MRU ghost                  否    最近从 MRU 淘汰的历史记录
  ───────────  ──────────────────  ───────────────────────────
   MFU ghost                  否    最近从 MFU 淘汰的历史记录

  Ghost 队列只记录 page_id，不保存页面数据，也不占 Frame。它的作用是判断“之前淘汰得是否合理”。

  ### 一次访问如何处理

  假设访问页面 P：

  1. P 在 MRU 或 MFU

  说明真正命中缓存。把它移动到 MFU 头部，因为它已经被重复访问。

  2. P 在 MRU ghost

  说明它之前因为“看起来只会用一次”而被淘汰，但现在又访问了。推论：应该给最近访问页面更多空间，因此增大 mru_target_size_，然后把 P 放入 MFU。

  3. P 在 MFU ghost

  说明一个曾经频繁使用的页面被淘汰后又访问了。推论：应该给频繁页面更多空间，因此减小 mru_target_size_，然后把 P 放入 MFU。

  4. 四个队列都没有 P

  这是全新页面，放到 MRU 头部。

  ### mru_target_size_ 是什么

  它不是 MRU 当前大小，而是 ARC 希望分配给 MRU 的容量。

  mru_target_size_ 变大：更保护最近访问的页面
  mru_target_size_ 变小：更保护频繁访问的页面

  ARC 根据 ghost 命中情况自动调整这个值，因此叫 Adaptive Replacement Cache。

  ### 淘汰逻辑

  需要腾出 Frame 时：

  MRU 实际大小 >= mru_target_size_
      → 优先从 MRU 尾部淘汰
  否则
      → 优先从 MFU 尾部淘汰

  只能淘汰 evictable == true 的 Frame。如果首选队列没有可淘汰项，再尝试另一个队列。被淘汰页面的 page_id 会进入对应 ghost 队列。

  例如：

  MRU: [C, B, A]

  左边是最新，右边最旧。淘汰 A 后：

  MRU:       [C, B]
  MRU ghost: [A]

  ### 为什么同时需要 frame_id 和 page_id

  - frame_id 表示内存槽位，用于告诉 Buffer Pool 淘汰哪个 Frame。
  - page_id 表示逻辑页面，用于在 ghost 队列中识别“被淘汰的页面又回来了”。

  同一个 Frame 后续可以装入不同 Page，所以两者不能混用。

  建议先把 RecordAccess() 的四种情况画成状态转换：

  全新页面       → MRU
  MRU 中再次访问 → MFU
  MFU 中再次访问 → MFU
  MRU ghost 命中 → MFU，并增大 MRU 目标
  MFU ghost 命中 → MFU，并减小 MRU 目标

  理解这张转换关系后，再看官网算法会清晰很多。


