# Order Book Component Development

> 目标：定义一个 **V1 版本的 C++ Order Book 组件**，先保证撮合逻辑正确、接口清晰、可测试，再在后续阶段进行低延迟优化。核心原则是：**Correctness First → Benchmark → Profile → Optimize**。

---

# 0. 系统需求

Order Book 组件用于维护当前市场中的买单（Bid）和卖单（Ask），接收外部传入的订单，并按照 **Price-Time Priority（价格优先、时间优先）** 规则进行撮合。V1 的目标不是一次性实现完整交易所，而是先建立一个行为正确、接口清晰、可测试、可 benchmark 的基础版本。

## 0.1 输入

Order Book V1 主要接收四类请求：**Add Order（新增订单）**、**Cancel Order（撤销订单）**、**Modify Order（修改订单）** 和 **Query（查询盘口状态）**。订单至少包含 `OrderId`、`Side`、`Price` 和 `Quantity` 四个字段，其中 `OrderId` 表示订单唯一编号，`Side` 表示买卖方向，`Price` 表示订单价格，`Quantity` 表示当前订单数量。

```cpp
struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};
```

V1 默认只实现 **Limit Order（限价单）**，其他订单类型放到后续版本再扩展。

---

## 0.2 输出

Order Book 对外产生的主要结果包括：**当前 Best Bid、当前 Best Ask、成交结果 Trade、订单剩余 Quantity、订单是否完全成交、Cancel / Modify 是否成功，以及当前 Order Book 状态**。成交结果可以抽象为下面的 `Trade`：

```cpp
struct Trade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
};
```

---

## 0.3 核心规则

Order Book 必须满足 **Price-Time Priority**。对于买单来说，价格越高优先级越高；对于卖单来说，价格越低优先级越高；如果多个订单价格相同，则先进入 Order Book 的订单先成交。

例如，同一个 `Price = 100` 的 Bid Price Level 中依次存在：

```text
Order #1 quantity=10
Order #2 quantity=20
Order #3 quantity=15
```

那么撮合顺序必须保持：

```text
#1 → #2 → #3
```

---

## 0.4 撮合条件

新的 Buy Order 只有在 `buy_price >= best_ask` 时才能与 Ask Book 中的订单成交；新的 Sell Order 只有在 `sell_price <= best_bid` 时才能与 Bid Book 中的订单成交。如果当前订单价格无法与对手盘交叉，则订单不会成交，而是作为 Resting Order 挂入对应的 Bid Book 或 Ask Book。

---

## 0.5 V1 范围

V1 实现：**Limit Order、Add、Cancel、Modify、Bid / Ask Book、Price-Time Priority、Partial Fill、Full Fill、Best Bid、Best Ask、Trade 生成和 Unit Test**。V1 暂时不实现：**Market Order、Stop Order、IOC / FOK、多交易品种、网络通信、多线程撮合、Lock-Free、Persistent Storage、Recovery、Exchange Protocol、Object Pool 和自定义 Allocator**。这些功能统一放到后续版本处理，避免一开始过度设计。

---

# 1. Order Book 组件的职责

Order Book 的核心职责是：**接收订单、维护买卖盘口、按照 Price-Time Priority 进行撮合、更新订单状态，并向外提供当前市场状态。** 整个处理过程可以概括为：`接收订单 → 检查订单 → 寻找对手盘 → 撮合 → 更新 Quantity → 删除完全成交订单 → 挂入未成交订单 → 更新盘口状态`。

## 1.1 保存买卖订单

Order Book 必须分别维护 **Bid Book（买盘）** 和 **Ask Book（卖盘）**。其中，Bid Book 保存所有买单，并按照价格从高到低排列，因此价格越高优先级越高；Ask Book 保存所有卖单，并按照价格从低到高排列，因此价格越低优先级越高。

```text
Bid Book                  Ask Book

100 ← Best Bid            101 ← Best Ask
 99                       102
 98                       103
 97                       104
```

因此可以直接理解为：**Best Bid 是当前市场最高买价，Best Ask 是当前市场最低卖价。**

---

## 1.2 管理 Price Level

Order Book 并不是简单保存一堆独立订单，而是首先按照 **Price（价格）** 对订单进行分组，同一个价格下的订单共同组成一个 **Price Level（价格档位）**。

```text
Price 100
├── Order #1
├── Order #2
└── Order #3

Price 99
├── Order #4
└── Order #5
```

因此，一个 Price Level 至少需要记录当前价格 `price`、该价格下所有订单以及该档位的总数量 `total_quantity`。整个 Order Book 的结构可以理解成：**Order Book → Bid / Ask → Price Level → Order**。

---

## 1.3 保证 Time Priority

当多个订单具有相同价格时，Order Book 必须继续按照 **Time Priority（时间优先）** 决定成交顺序，也就是谁先进入 Order Book，谁先成交。例如同一个 `Price = 100` 的档位中依次存在 `Order #1、Order #2、Order #3`，那么撮合顺序必须保持为 `#1 → #2 → #3`，后来进入的订单不能插到前面。

---

## 1.4 撮合买卖订单

当新的订单进入 Order Book 后，需要先检查它是否能够与对手盘成交。对于新的 **Buy Order**，当 `buy_price >= best_ask` 时可以与卖盘成交；对于新的 **Sell Order**，当 `sell_price <= best_bid` 时可以与买盘成交。如果当前价格无法与对手盘交叉，则订单不会成交，而是作为 Resting Order 挂入对应的 Bid Book 或 Ask Book。

---

## 1.5 支持 Partial Fill 和 Full Fill

Order Book 必须同时支持 **Partial Fill（部分成交）** 和 **Full Fill（完全成交）**。例如当前存在 `Ask 100 × 30`，此时进入 `Buy 100 × 50`，那么首先成交 `30`，原 Ask 被完全删除，而 Buy 订单仍然剩余 `20`；如果没有其他可以继续成交的 Ask，那么剩余的 `Buy 100 × 20` 就进入 Bid Book。相反，如果买卖双方 Quantity 都是 `30`，则成交后双方都被完全删除。

---

## 1.6 支持 Cancel

`cancel_order(order_id)` 用于撤销当前仍然存在于 Order Book 中的订单。Order Book 根据 `OrderId` 找到订单后，需要从对应 Price Level 中删除该订单，同时更新 `total_quantity`；如果删除后该 Price Level 已经没有任何订单，则整个 Price Level 也应该被删除。

---

## 1.7 支持 Modify

V1 中可以把 Modify 简化为 **`Cancel Old Order + Add New Order`**。也就是说，当订单的 `price` 或 `quantity` 被修改时，先删除原订单，再把修改后的订单重新加入 Order Book，因此它会重新获得新的 Time Priority。后续如果需要模拟真实交易所规则，再进一步区分“减少 Quantity 是否保留时间优先级”“增加 Quantity 是否重新排队”等情况。

---

## 1.8 提供 Market State 查询

Order Book 还需要向其他组件提供当前盘口状态，例如 `best_bid()` 获取最高买价、`best_ask()` 获取最低卖价、`size()` 获取当前 Active Order 数量、`empty()` 判断盘口是否为空，以及 `contains(order_id)` 判断某个订单是否仍然存在。这些接口后续会被 Strategy、Risk、Test 和 Benchmark 使用。

---

# 2. 成员变量

Order Book V1 的核心成员变量可以控制在 **`bids_`、`asks_` 和 `orders_`** 三个主要结构上；`next_order_id_` 和 `trade_count_` 属于辅助状态。V1 暂时不建议额外维护 `best_bid_` 和 `best_ask_` 缓存，因为有序 Bid / Ask 容器本身已经可以获得最优价格，过早缓存反而会增加状态同步复杂度。

## 2.1 `bids_`

`bids_` 用来保存所有 **买单 Price Level**，并按照价格从高到低排列，因此容器最前面的 Price Level 就是当前 **Best Bid**。V1 可以先使用 `std::map<Price, PriceLevel, std::greater<Price>>` 作为 baseline，因为它实现简单、排序规则明确，也方便先验证撮合逻辑是否正确。

```cpp
std::map<Price, PriceLevel, std::greater<Price>> bids_;
```

例如：

```text
bids_

100 ← Best Bid
99
98
97
```

---

## 2.2 `asks_`

`asks_` 用来保存所有 **卖单 Price Level**，并按照价格从低到高排列，因此容器最前面的 Price Level 就是当前 **Best Ask**。V1 可以使用默认升序的 `std::map<Price, PriceLevel>`。

```cpp
std::map<Price, PriceLevel> asks_;
```

例如：

```text
asks_

101 ← Best Ask
102
103
104
```

---

## 2.3 `orders_`

`orders_` 是一个 **Order Index（订单索引）**，主要作用是通过 `OrderId` 快速定位某一张 Active Order，从而支持 `cancel_order()`、`modify_order()`、`contains()` 等操作。如果没有这个索引，每次撤单都可能需要遍历整个 Bid Book 或 Ask Book。

V1 可以先设计成：

```cpp
std::unordered_map<OrderId, OrderLocation> orders_;
```

其中 `OrderLocation` 至少可以记录订单所在的 Side 和 Price：

```cpp
struct OrderLocation {
    Side side;
    Price price;
};
```

这样可以先通过 `OrderId` 找到订单属于 Bid 还是 Ask，再根据 Price 找到对应 Price Level。后续做低延迟优化时，再考虑让 `orders_` 直接保存 iterator 或 pointer，从而减少二次查找。

---

## 2.4 `next_order_id_`

如果 Order Book 自己负责生成订单编号，可以维护 `OrderId next_order_id_{1};`，每加入一张新订单后递增，用来保证 OrderId 唯一。如果未来订单编号直接由外部 Exchange Gateway 或客户端传入，那么这个成员变量就可以删除。

```cpp
OrderId next_order_id_{1};
```

---

## 2.5 `trade_count_`

`trade_count_` 用来记录 Order Book 到目前为止已经生成多少次成交，主要服务于 **Test、Debug、Benchmark 和 Statistics**，并不是撮合逻辑必须依赖的核心状态，因此如果第一版暂时没有统计需求，也可以不加入。

```cpp
std::uint64_t trade_count_{0};
```

---

## 2.6 `best_bid_` 和 `best_ask_`

V1 暂时**不建议**单独保存 `best_bid_` 和 `best_ask_`。因为我们已经使用有序容器，所以可以直接通过 `bids_.begin()` 和 `asks_.begin()` 获取 Best Bid 和 Best Ask。否则每次 `Add、Cancel、Modify、Full Fill、Price Level 删除` 时都需要同时维护缓存，很容易出现缓存值和真实 Order Book 状态不一致的问题。等后面 benchmark 证明这里真的存在性能瓶颈，再考虑添加缓存。

---

## 2.7 最终成员变量结构

因此 V1 可以先保持成非常简单的结构：

```cpp
class OrderBook {
private:
    BidLevels bids_;
    AskLevels asks_;
    OrderIndex orders_;

    OrderId next_order_id_{1};
    std::uint64_t trade_count_{0};
};
```

其中真正决定 Order Book 核心行为的是 **`bids_ + asks_ + orders_`**：`bids_` 负责维护买盘，`asks_` 负责维护卖盘，`orders_` 负责通过 OrderId 快速定位订单；其他变量都是围绕这三个核心结构提供辅助功能。

---

# 3. 成员函数

Order Book V1 的成员函数可以分成两类：**Public Interface** 负责对外提供 Add、Cancel、Modify 和 Query；**Private Helper Functions** 负责内部撮合、插入和删除。这样既能保持接口清晰，也能避免把所有逻辑塞进一个巨大的 `add_order()` 中。

## 3.1 `add_order()`

`add_order()` 是 Order Book 最核心的入口函数，用来接收一张新订单。它首先验证订单是否合法，然后根据 Side 决定执行 `match_buy()` 或 `match_sell()`；如果订单没有完全成交，则把剩余部分作为 Resting Order 挂入对应的 Bid Book 或 Ask Book，最后返回本次产生的所有 Trade。

```cpp
auto add_order(Order order) -> std::vector<Trade>;
```

整体流程：

```text
add_order(order)
      ↓
validate
      ↓
BUY / SELL
      ↓
match
      ↓
remaining quantity > 0 ?
      ↓
YES → insert_resting_order()
NO  → return trades
```

---

## 3.2 `cancel_order()`

`cancel_order()` 根据 `OrderId` 撤销一张仍然存在于 Order Book 中的订单。它先通过 `orders_` 找到订单所在的 Side 和 Price，再定位 Price Level，删除订单并更新 `total_quantity`；如果该 Price Level 已经没有任何订单，则继续删除整个 Price Level。正常撤单返回 `true`，找不到订单则返回 `false`。

```cpp
auto cancel_order(OrderId id) -> bool;
```

---

## 3.3 `modify_order()`

`modify_order()` 用于修改已有订单的 Price 或 Quantity。V1 采用简单明确的规则：**Modify = Cancel Old Order + Add New Order**，因此修改后的订单重新获得新的 Time Priority。这样实现逻辑简单，也能避免一开始引入复杂的交易所优先级规则。

```cpp
auto modify_order(
    OrderId id,
    Price new_price,
    Quantity new_quantity
) -> bool;
```

---

## 3.4 `best_bid()`

`best_bid()` 返回当前 Bid Book 中的最高买价。如果 Bid Book 为空，则返回 `std::nullopt`。由于 `bids_` 本身按照价格从高到低排序，因此 V1 可以直接从 `bids_.begin()` 获取 Best Bid。

```cpp
auto best_bid() const -> std::optional<Price>;
```

---

## 3.5 `best_ask()`

`best_ask()` 返回当前 Ask Book 中的最低卖价。如果 Ask Book 为空，则返回 `std::nullopt`。由于 `asks_` 按照价格从低到高排序，因此 V1 可以直接从 `asks_.begin()` 获取 Best Ask。

```cpp
auto best_ask() const -> std::optional<Price>;
```

---

## 3.6 `contains()`

`contains()` 用来判断某个 `OrderId` 是否仍然存在于 Order Book 中。V1 可以直接查询 `orders_`，不需要扫描整个 Bid Book 或 Ask Book。

```cpp
auto contains(OrderId id) const -> bool;
```

---

## 3.7 `size()`

`size()` 返回当前挂在 Order Book 中的 **Active Order 总数**。这里统计的是订单数量，而不是 Price Level 数量，也不是 Trade 数量。

```cpp
auto size() const -> std::size_t;
```

---

## 3.8 `empty()`

`empty()` 用来判断当前 Order Book 是否没有任何 Active Order。只要 Bid Book 和 Ask Book 都为空，就可以认为 Order Book 为空。

```cpp
auto empty() const -> bool;
```

---

## 3.9 `clear()`

`clear()` 用来清空 `bids_`、`asks_` 和 `orders_`，主要用于 Unit Test 和 Simulation Reset。真实交易环境中是否保留这个接口，可以等后续系统边界确定后再决定。

```cpp
auto clear() -> void;
```

---

## 3.10 `match_buy()`

`match_buy()` 负责处理新的 Buy Order。它不断检查 Best Ask，只要 `buy.price >= best_ask` 且 Buy Order 仍有剩余 Quantity，就从最优 Ask Price Level 的最早订单开始撮合。每次成交量为 `min(buy.quantity, sell.quantity)`；Resting Sell 完全成交后删除该订单，如果整个 Price Level 为空则删除 Price Level；如果 Buy Order 仍有剩余，则继续检查下一张 Sell Order 或下一个 Price Level。

```cpp
auto match_buy(Order& order) -> std::vector<Trade>;
```

---

## 3.11 `match_sell()`

`match_sell()` 与 `match_buy()` 对称，用来处理新的 Sell Order。只要 `sell.price <= best_bid` 且 Sell Order 仍有剩余 Quantity，就从当前 Best Bid Price Level 中最早进入的订单开始撮合，并持续到 Sell Order 完全成交或已经没有满足价格条件的 Bid。

```cpp
auto match_sell(Order& order) -> std::vector<Trade>;
```

---

## 3.12 `insert_resting_order()`

`insert_resting_order()` 用来把没有完全成交的剩余订单挂入 Bid Book 或 Ask Book。它需要找到或创建对应的 Price Level，把订单追加到该 Price Level 的尾部以保持 Time Priority，同时更新 `total_quantity` 和 `orders_`。

```cpp
auto insert_resting_order(Order order) -> void;
```

---

## 3.13 `erase_order()`

`erase_order()` 用来统一处理订单删除逻辑，包括从 Price Level 中删除订单、更新 `total_quantity`、必要时删除空 Price Level，以及同步删除 `orders_` 中的索引。统一删除逻辑可以避免 Cancel 和 Match 各自维护一套容易不一致的状态更新代码。

```cpp
auto erase_order(OrderId id) -> void;
```

---

## 3.14 Order Book 对外接口概览

```cpp
class OrderBook {
public:
    auto add_order(Order order) -> std::vector<Trade>;

    auto cancel_order(OrderId id) -> bool;

    auto modify_order(
        OrderId id,
        Price new_price,
        Quantity new_quantity
    ) -> bool;

    [[nodiscard]]
    auto best_bid() const -> std::optional<Price>;

    [[nodiscard]]
    auto best_ask() const -> std::optional<Price>;

    [[nodiscard]]
    auto contains(OrderId id) const -> bool;

    [[nodiscard]]
    auto size() const -> std::size_t;

    [[nodiscard]]
    auto empty() const -> bool;

    auto clear() -> void;

private:
    auto match_buy(Order& order) -> std::vector<Trade>;
    auto match_sell(Order& order) -> std::vector<Trade>;
    auto insert_resting_order(Order order) -> void;
    auto erase_order(OrderId id) -> void;

private:
    BidLevels bids_;
    AskLevels asks_;
    OrderIndex orders_;

    OrderId next_order_id_{1};
    std::uint64_t trade_count_{0};
};
```

这只是 V1 的接口设计，不要求最终实现必须逐字一致；Codex 在实现时可以在不改变组件职责和核心行为的前提下做小范围调整。

---

# 4. 异常和边界情况

Order Book 是一个状态非常密集的组件，因此边界情况不能等代码写完后再补。V1 至少需要提前定义空盘口、非法输入、重复 OrderId、Partial Fill、Full Fill、跨多个 Price Level 撮合、FIFO、空 Price Level 删除以及内部状态一致性等行为。

## 4.1 空 Order Book

新建 Order Book 后，`bids_` 和 `asks_` 都应该为空，因此 `best_bid() == std::nullopt`、`best_ask() == std::nullopt`、`empty() == true`、`size() == 0`。

---

## 4.2 Quantity = 0

`Quantity = 0` 的订单没有实际交易意义，因此 V1 应直接拒绝。推荐规则是：**invalid input → exception；normal business failure → bool / optional**。因此零 Quantity 可以抛出 `std::invalid_argument`，而 Cancel 不存在的订单则返回 `false`。

---

## 4.3 非法 Price

如果当前 `Price` 类型允许产生 `price <= 0` 的值，那么 V1 应拒绝该订单。后续可以进一步使用强类型 `Price` 和 `Quantity` 限制非法状态，但 V1 不需要为了这个目标提前加入复杂 abstraction。

---

## 4.4 Duplicate OrderId

如果某个 `OrderId` 已经存在于 Order Book 中，再收到相同 ID 的新订单时必须拒绝，否则 `orders_` 会出现索引冲突，同时也会破坏 Cancel 和 Modify 的正确性。

---

## 4.5 Cancel / Modify 不存在的订单

调用 `cancel_order(unknown_id)` 或 `modify_order(unknown_id, ...)` 时，推荐直接返回 `false`，因为“找不到订单”属于正常业务失败，不需要使用 exception。

---

## 4.6 新订单完全成交

如果 `Ask 100 × 20` 已经存在，此时加入 `Buy 101 × 20`，那么双方完全成交后，Incoming Buy 不应该进入 `bids_`，原 Ask 也应该从 `asks_` 和 `orders_` 中删除，不能留下任何残余状态。

---

## 4.7 新订单部分成交

如果已有 `Ask 100 × 20`，此时加入 `Buy 100 × 50`，那么先成交 `20`，Ask 被删除，而 Buy 剩余 `30`。如果没有其他可成交 Ask，则剩余的 `Buy 100 × 30` 必须进入 Bid Book。

---

## 4.8 Resting Order 部分成交

如果已有 `Ask #1 100 × 50`，此时加入 `Buy 100 × 20`，成交后 `Ask #1` 仍剩余 `30`，它不能被删除，`OrderId` 不变，原有 Time Priority 也必须保留。

---

## 4.9 一个订单跨多个 Price Level 撮合

Incoming Order 可能一次穿透多个 Price Level。例如 Ask Book 为 `100 × 10、101 × 20、102 × 30`，此时加入 `Buy 102 × 50`，必须依次成交 `100 × 10 → 101 × 20 → 102 × 20`，总成交量达到 `50` 后停止，而不是只处理 Best Ask 一个 Price Level。

---

## 4.10 相同 Price 下 FIFO

如果 `Ask 100` 下依次存在 `#1 × 10、#2 × 10、#3 × 10`，此时加入 `Buy 100 × 15`，必须先让 `#1` 完全成交，再让 `#2` 成交 `5`，不能跳过更早进入的订单。

---

## 4.11 Price Level 清空

当某个 Price Level 的最后一张订单因为 Full Fill 或 Cancel 被删除后，该 Price Level 本身也必须从对应 Book 中删除，否则会留下空 Price Level，并可能导致 Best Bid / Best Ask 返回错误结果。

---

## 4.12 `orders_` 与 Book 状态一致性

系统必须长期保持一个重要 invariant：**存在于 `bids_ / asks_` 中的每个 Active Order 都必须存在于 `orders_` 中；而 `orders_` 中的每个 OrderId 也必须能够在实际 Book 中找到。** Add、Cancel、Modify、Partial Fill 和 Full Fill 后都必须维持这个一致性。

---

## 4.13 `total_quantity` 一致性

任何 Price Level 都必须满足：`total_quantity == 该 Price Level 下所有 Active Order 的 remaining quantity 之和`。例如同一个价格下存在 `10、20、30` 三张订单，则 `total_quantity` 必须等于 `60`。Add、Cancel、Match 和 Modify 后都需要同步更新。

---

## 4.14 Quantity 溢出

如果 Quantity 和 `total_quantity` 使用整数类型，需要考虑累加时的 overflow。V1 可以先使用足够大的无符号整数类型，并在 Debug / Test 中覆盖边界值；精确的最大范围等后续 Exchange Protocol 确定后再约束。

---

# 5. 测试

Order Book V1 的测试重点是 **Correctness**，可以分成 Unit Test、Matching Test 和 Invariant Test。性能测试不能替代功能测试；只有在这些行为全部正确后，才进入 Benchmark 和 Profiling 阶段。

## 5.1 Empty Book

新建 `OrderBook` 后，验证 `empty() == true`、`size() == 0`、`best_bid() == std::nullopt`、`best_ask() == std::nullopt`。

---

## 5.2 Add Single Bid / Ask

加入 `Buy 100 × 10` 后，验证 `best_bid() == 100`、`best_ask() == std::nullopt`、`size() == 1`；加入 `Sell 101 × 10` 后，再验证 `best_ask() == 101`。

---

## 5.3 Best Bid / Best Ask Ordering

对于 Bid，依次加入 `98、100、99` 后验证 `best_bid() == 100`；对于 Ask，依次加入 `103、101、102` 后验证 `best_ask() == 101`。这个测试用于确认两个 Book 的排序方向正确。

---

## 5.4 Non-Crossing Orders

已有 `Ask = 101` 时加入 `Buy = 100`，因为 `100 < 101`，双方不能成交。测试应验证 Bid 仍为 `100`、Ask 仍为 `101`，且没有生成 Trade。

---

## 5.5 Exact Match

已有 `Ask 100 × 20` 时加入 `Buy 100 × 20`，应生成一笔 `quantity = 20` 的 Trade，成交后双方都从 Order Book 中删除，Book 重新为空。

---

## 5.6 Buy Crosses Ask / Sell Crosses Bid

已有 `Ask 100 × 20` 时加入 `Buy 101 × 20` 应发生成交；已有 `Bid 100 × 20` 时加入 `Sell 99 × 20` 也应发生成交。这个测试用于验证 crossed market 的判断条件正确。

---

## 5.7 Incoming Order Partial Fill

已有 `Ask 100 × 20` 时加入 `Buy 100 × 50`，应成交 `20`，Ask 删除，Incoming Buy 剩余 `30` 并进入 Bid Book；最终验证 `best_bid() == 100`。

---

## 5.8 Resting Order Partial Fill

已有 `Ask #1 100 × 50` 时加入 `Buy 100 × 20`，成交后 `Ask #1` 应仍存在且剩余 `30`，原有 OrderId 和 Time Priority 不变。

---

## 5.9 Multiple Price Level Match

Ask Book 为 `100 × 10、101 × 20、102 × 30` 时加入 `Buy 102 × 50`，验证成交顺序为 `100 × 10 → 101 × 20 → 102 × 20`，最终 `102` Price Level 仍剩余 `10`。

---

## 5.10 FIFO Test

同一个 `Ask 100` Price Level 中依次插入 `#1 × 10、#2 × 10、#3 × 10`，再加入 `Buy 100 × 15`，验证 `#1` 完全成交、`#2` 剩余 `5`、`#3` 仍为 `10`。这个测试直接验证 Time Priority。

---

## 5.11 Cancel Order

加入 `Buy #1 100 × 10` 后调用 `cancel_order(#1)`，验证返回 `true`，同时 `#1` 不再存在、`size() == 0`、`best_bid() == std::nullopt`。

---

## 5.12 Cancel Unknown Order

调用 `cancel_order(999)` 时应返回 `false`，同时 Order Book 中原有状态保持不变。

---

## 5.13 Cancel Best Bid / Best Ask

如果 Bid Book 为 `101、100、99`，撤销 `101` 后应得到 `best_bid() == 100`；如果 Ask Book 为 `101、102、103`，撤销 `101` 后应得到 `best_ask() == 102`。

---

## 5.14 Modify Order

已有 `Buy #1 100 × 10`，调用 Modify 把它改成 `price = 101、quantity = 20` 后，验证旧状态被删除、新状态正确插入，并且 `best_bid() == 101`。按照 V1 规则，修改后的订单重新获得新的 Time Priority。

---

## 5.15 Duplicate OrderId

连续加入两个相同 `OrderId` 的订单，第二次 Add 应被拒绝，并且第一张订单的状态不能受到破坏。

---

## 5.16 Invalid Quantity / Price

尝试加入 `quantity = 0` 或非法 Price 的订单时，应按照 V1 输入规则拒绝，同时保证 Book State 不发生变化。

---

## 5.17 Price Level Removal

当某个 Price Level 的最后一张订单因为 Full Fill 或 Cancel 被删除后，验证该 Price Level 也被删除，同时 Best Bid / Best Ask 正确切换到下一档。

---

## 5.18 Order Index Consistency

经过多次 `Add → Match → Cancel → Modify` 后，验证 `orders_.size()` 与实际 Active Order 数量一致，并保证 `orders_` 中的每一个 OrderId 都能在 Bid Book 或 Ask Book 中找到。

---

## 5.19 Total Quantity Consistency

对于每一个 Price Level，验证 `total_quantity == sum(order.remaining_quantity)`。这个 invariant 在 Add、Partial Fill、Full Fill、Cancel 和 Modify 之后都必须成立。

---

# V1 完成标准

Order Book Component 可以认为 V1 完成，当它已经能够：**添加 Bid / Ask、维护正确的价格优先级、在相同价格下保持 FIFO、执行 Buy→Ask 和 Sell→Bid 撮合、支持 Full Fill / Partial Fill / 跨多个 Price Level 撮合、支持 Cancel / Modify、提供 Best Bid / Best Ask，并长期保持 `orders_` 与 Book 状态一致以及 Price Level `total_quantity` 一致。** 同时要求所有 Unit Tests 通过，并且 Debug / Release 都能够成功 Build。

可以用下面的 Checklist 做最终确认：

- [ ] 可以添加 Bid
- [ ] 可以添加 Ask
- [ ] Bid 按价格从高到低保持优先级
- [ ] Ask 按价格从低到高保持优先级
- [ ] 同价格满足 FIFO
- [ ] 可以进行 Buy → Ask 撮合
- [ ] 可以进行 Sell → Bid 撮合
- [ ] 支持 Full Fill
- [ ] 支持 Partial Fill
- [ ] 支持跨多个 Price Level 撮合
- [ ] 支持 Cancel
- [ ] 支持 Modify
- [ ] 支持 Best Bid
- [ ] 支持 Best Ask
- [ ] `orders_` 与实际 Book 状态一致
- [ ] Price Level `total_quantity` 一致
- [ ] 空 Price Level 会自动删除
- [ ] 所有 Unit Tests 通过
- [ ] Debug / Release 都能够 Build

---

# 后续优化方向

V1 完成后不直接重写，而是先建立 **Baseline Benchmark**，再通过 Profiling 找到真正瓶颈。可能的后续优化方向包括：把 `std::map` 替换为更连续的 price storage、把通用订单容器替换为 intrusive structure、让 `orders_` 直接保存 Order pointer / iterator、通过 pre-allocation 或 object pool 减少 heap allocation，以及进一步优化 Memory Layout、CPU Cache、Branch Prediction、Pointer Chasing 和 Tail Latency。

优化流程应保持：

```text
Baseline
   ↓
Profile
   ↓
找到瓶颈
   ↓
提出假设
   ↓
修改一个主要变量
   ↓
Benchmark Again
```

这些内容属于 **V2 Performance Optimization**，不应该在 V1 正确性尚未验证时提前加入。

---

# 最终组件数据流

```text
External Order
      │
      ▼
OrderBook::add_order()
      │
      ▼
Validate
      │
      ▼
Buy / Sell
      │
      ▼
Match Opposite Book
      │
      ├───────────────┐
      │               │
      ▼               ▼
Full Fill        Partial / No Fill
      │               │
      │               ▼
      │        Insert Remaining Order
      │               │
      └───────┬───────┘
              ▼
       Update Book State
              │
              ▼
      Maintain Invariants
              │
              ▼
        Return Trades
```

这份文档定义的是 **Order Book Component 的 V1 开发契约**。下一阶段应该基于本文档继续确定 `PriceLevel` 内部的数据结构、`orders_` 的定位方式，以及 Add / Cancel / Match 的复杂度目标，然后再让 Codex 生成最小实现计划并开始写代码。
