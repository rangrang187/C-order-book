# C++ Order Book

A correctness-first C++20 limit order book based on the repository's V1
development manual. The implementation deliberately uses standard containers as
a baseline; profiling and low-latency data-structure work belong to V2.

## V1 behavior

- Limit orders with externally supplied, non-zero order IDs.
- Price-time priority: highest bid, lowest ask, then FIFO within a price level.
- Full fills, partial fills, and matching across multiple price levels.
- Add, cancel, and modify (`cancel + add`, so time priority is reset).
- Best bid/ask, active-order lookup, level quantity, size, and trade count queries.
- Checked invalid input, duplicate active IDs, and price-level quantity overflow.
- Debug assertions and a public test helper for book/index/aggregate invariants.

Trades execute at the resting (maker) order's price. Active order IDs must be
unique; an ID may be reused after its previous order leaves the book. The manual's
suggested `bool` modify result is extended to `ModifyResult { modified, trades }`
so a repriced order can cross the spread without losing its execution details.

## Public API

The component is declared in `src/order_book.h` under namespace `orderbook`:

```cpp
OrderBook book;

auto trades = book.add_order(Order{1, Side::Buy, 100, 10});
bool cancelled = book.cancel_order(1);
auto modification = book.modify_order(1, 101, 20);

if (modification.modified) {
    for (const auto& trade : modification.trades) {
        // Publish or otherwise process each execution.
    }
}

auto bid = book.best_bid();
auto ask = book.best_ask();
auto order = book.get_order(1);
```

`add_order()` throws `std::invalid_argument` for zero IDs, non-positive prices,
zero quantities, invalid sides, and duplicate active IDs. Cancel or modify of an
unknown ID returns `false`.

## Build

Requirements: CMake 3.20 or newer and a C++20 compiler.

```powershell
cmake -S . -B build
cmake --build build --config Debug
cmake --build build --config Release
```

Run the example:

```powershell
.\build\Release\order_book_demo.exe
```

## Test

The zero-dependency test runner covers all 19 V1 cases from the development
manual plus symmetric and boundary scenarios (25 deterministic cases in total),
including FIFO, both matching directions, partial/full fills, multi-level matching,
cancel/modify behavior, invalid input, overflow, and invariants.

```powershell
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure

# Show every individual scenario
.\build\Release\order_book_tests.exe
```

## Project structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- order_book_component_development_reformatted.md
|-- src
|   |-- main.cpp
|   |-- order_book.cpp
|   `-- order_book.h
`-- tests
    `-- order_book_tests.cpp
```
