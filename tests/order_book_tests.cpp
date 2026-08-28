#include "order_book.h"

#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using orderbook::Order;
using orderbook::OrderBook;
using orderbook::OrderId;
using orderbook::Price;
using orderbook::Quantity;
using orderbook::Side;
using orderbook::Trade;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

auto fail(std::string_view expression, std::string_view file, int line) -> void {
    std::ostringstream message;
    message << file << ':' << line << ": check failed: " << expression;
    throw TestFailure(message.str());
}

#define CHECK(...)                                                               \
    do {                                                                         \
        if (!(__VA_ARGS__)) {                                                    \
            fail(#__VA_ARGS__, __FILE__, __LINE__);                              \
        }                                                                        \
    } while (false)

template <typename Exception, typename Action>
auto check_throws(Action&& action, std::string_view expression, std::string_view file,
                  int line) -> void {
    try {
        std::invoke(std::forward<Action>(action));
    } catch (const Exception&) {
        return;
    } catch (...) {
        fail(expression, file, line);
    }
    fail(expression, file, line);
}

#define CHECK_THROWS_AS(expression, exception_type)                              \
    check_throws<exception_type>([&] { static_cast<void>(expression); },          \
                                 #expression, __FILE__, __LINE__)

[[nodiscard]] auto buy(OrderId id, Price price, Quantity quantity) -> Order {
    return Order{id, Side::Buy, price, quantity};
}

[[nodiscard]] auto sell(OrderId id, Price price, Quantity quantity) -> Order {
    return Order{id, Side::Sell, price, quantity};
}

auto test_empty_book() -> void {
    const OrderBook book;
    CHECK(book.empty());
    CHECK(book.size() == 0);
    CHECK(book.best_bid() == std::nullopt);
    CHECK(book.best_ask() == std::nullopt);
    CHECK(book.trade_count() == 0);
    CHECK(book.validate_invariants());
}

auto test_add_single_bid_and_ask() -> void {
    OrderBook book;
    CHECK(book.add_order(buy(1, 100, 10)).empty());
    CHECK(book.best_bid() == std::optional<Price>{100});
    CHECK(book.best_ask() == std::nullopt);
    CHECK(book.size() == 1);

    CHECK(book.add_order(sell(2, 101, 10)).empty());
    CHECK(book.best_ask() == std::optional<Price>{101});
    CHECK(book.size() == 2);
    CHECK(book.validate_invariants());
}

auto test_best_price_ordering() -> void {
    OrderBook book;
    CHECK(book.add_order(buy(1, 98, 1)).empty());
    CHECK(book.add_order(buy(2, 100, 1)).empty());
    CHECK(book.add_order(buy(3, 99, 1)).empty());
    CHECK(book.add_order(sell(4, 103, 1)).empty());
    CHECK(book.add_order(sell(5, 101, 1)).empty());
    CHECK(book.add_order(sell(6, 102, 1)).empty());

    CHECK(book.best_bid() == std::optional<Price>{100});
    CHECK(book.best_ask() == std::optional<Price>{101});
    CHECK(book.validate_invariants());
}

auto test_non_crossing_orders() -> void {
    OrderBook book;
    CHECK(book.add_order(sell(1, 101, 10)).empty());
    CHECK(book.add_order(buy(2, 100, 10)).empty());
    CHECK(book.best_bid() == std::optional<Price>{100});
    CHECK(book.best_ask() == std::optional<Price>{101});
    CHECK(book.size() == 2);
    CHECK(book.validate_invariants());
}

auto test_exact_match() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 20)));

    const auto trades = book.add_order(buy(2, 100, 20));
    CHECK(trades == std::vector<Trade>{{2, 1, 100, 20}});
    CHECK(book.empty());
    CHECK(!book.contains(1));
    CHECK(!book.contains(2));
    CHECK(book.validate_invariants());
}

auto test_crossed_marketable_limits() -> void {
    OrderBook buy_cross;
    static_cast<void>(buy_cross.add_order(sell(1, 100, 20)));
    CHECK(buy_cross.add_order(buy(2, 101, 20)) ==
          std::vector<Trade>{{2, 1, 100, 20}});

    OrderBook sell_cross;
    static_cast<void>(sell_cross.add_order(buy(3, 100, 20)));
    CHECK(sell_cross.add_order(sell(4, 99, 20)) ==
          std::vector<Trade>{{3, 4, 100, 20}});
    CHECK(buy_cross.validate_invariants());
    CHECK(sell_cross.validate_invariants());
}

auto test_incoming_order_partial_fill() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 20)));
    CHECK(book.add_order(buy(2, 100, 50)) ==
          std::vector<Trade>{{2, 1, 100, 20}});

    CHECK(!book.contains(1));
    CHECK(book.get_order(2) == std::optional<Order>{buy(2, 100, 30)});
    CHECK(book.bid_quantity_at(100) == 30);
    CHECK(book.best_bid() == std::optional<Price>{100});
    CHECK(book.best_ask() == std::nullopt);
    CHECK(book.validate_invariants());
}

auto test_resting_order_partial_fill() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 50)));
    CHECK(book.add_order(buy(2, 100, 20)) ==
          std::vector<Trade>{{2, 1, 100, 20}});

    CHECK(book.get_order(1) == std::optional<Order>{sell(1, 100, 30)});
    CHECK(book.ask_quantity_at(100) == 30);
    CHECK(!book.contains(2));
    CHECK(book.validate_invariants());
}

auto test_multiple_price_level_match() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 10)));
    static_cast<void>(book.add_order(sell(2, 101, 20)));
    static_cast<void>(book.add_order(sell(3, 102, 30)));

    const auto trades = book.add_order(buy(4, 102, 50));
    CHECK(trades == std::vector<Trade>({
                        {4, 1, 100, 10},
                        {4, 2, 101, 20},
                        {4, 3, 102, 20},
                    }));
    CHECK(book.get_order(3) == std::optional<Order>{sell(3, 102, 10)});
    CHECK(book.best_ask() == std::optional<Price>{102});
    CHECK(book.ask_quantity_at(102) == 10);
    CHECK(book.validate_invariants());
}

auto test_fifo_at_same_price() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 10)));
    static_cast<void>(book.add_order(sell(2, 100, 10)));
    static_cast<void>(book.add_order(sell(3, 100, 10)));

    const auto trades = book.add_order(buy(4, 100, 15));
    CHECK(trades == std::vector<Trade>({
                        {4, 1, 100, 10},
                        {4, 2, 100, 5},
                    }));
    CHECK(!book.contains(1));
    CHECK(book.get_order(2) == std::optional<Order>{sell(2, 100, 5)});
    CHECK(book.get_order(3) == std::optional<Order>{sell(3, 100, 10)});
    CHECK(book.ask_quantity_at(100) == 15);
    CHECK(book.validate_invariants());
}

auto test_cancel_order() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 10)));
    CHECK(book.cancel_order(1));
    CHECK(!book.contains(1));
    CHECK(book.size() == 0);
    CHECK(book.best_bid() == std::nullopt);
    CHECK(book.validate_invariants());
}

auto test_cancel_unknown_order() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 10)));
    CHECK(!book.cancel_order(999));
    CHECK(book.get_order(1) == std::optional<Order>{buy(1, 100, 10)});
    CHECK(book.validate_invariants());
}

auto test_cancel_best_prices() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 101, 1)));
    static_cast<void>(book.add_order(buy(2, 100, 1)));
    static_cast<void>(book.add_order(buy(3, 99, 1)));
    static_cast<void>(book.add_order(sell(4, 102, 1)));
    static_cast<void>(book.add_order(sell(5, 103, 1)));

    CHECK(book.cancel_order(1));
    CHECK(book.best_bid() == std::optional<Price>{100});
    CHECK(book.cancel_order(4));
    CHECK(book.best_ask() == std::optional<Price>{103});
    CHECK(book.validate_invariants());
}

auto test_modify_requeues_order() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 10)));
    static_cast<void>(book.add_order(buy(2, 100, 10)));

    const auto modification = book.modify_order(1, 100, 5);
    CHECK(modification.modified);
    CHECK(modification.trades.empty());
    const auto trades = book.add_order(sell(3, 100, 12));
    CHECK(trades == std::vector<Trade>({
                        {2, 3, 100, 10},
                        {1, 3, 100, 2},
                    }));
    CHECK(book.get_order(1) == std::optional<Order>{buy(1, 100, 3)});
    const auto unknown = book.modify_order(999, 101, 1);
    CHECK(!unknown);
    CHECK(unknown.trades.empty());
    CHECK(book.validate_invariants());
}

auto test_duplicate_order_id() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 99, 10)));
    static_cast<void>(book.add_order(sell(2, 100, 5)));
    CHECK_THROWS_AS(book.add_order(buy(1, 100, 5)), std::invalid_argument);
    CHECK(book.get_order(1) == std::optional<Order>{buy(1, 99, 10)});
    CHECK(book.get_order(2) == std::optional<Order>{sell(2, 100, 5)});
    CHECK(book.trade_count() == 0);
    CHECK(book.size() == 2);
    CHECK(book.validate_invariants());
}

auto test_invalid_order_fields() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 10)));

    CHECK_THROWS_AS(book.add_order(buy(2, 100, 0)), std::invalid_argument);
    CHECK_THROWS_AS(book.add_order(buy(3, 0, 1)), std::invalid_argument);
    CHECK_THROWS_AS(book.add_order(buy(3, -1, 1)), std::invalid_argument);
    CHECK_THROWS_AS(book.add_order(buy(0, 100, 1)), std::invalid_argument);
    CHECK_THROWS_AS(
        book.add_order(Order{4, static_cast<Side>(99), 100, 1}),
        std::invalid_argument);
    CHECK_THROWS_AS(book.modify_order(1, 0, 5), std::invalid_argument);

    CHECK(book.get_order(1) == std::optional<Order>{buy(1, 100, 10)});
    CHECK(book.size() == 1);
    CHECK(book.validate_invariants());
}

auto test_price_level_removal() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 10)));
    static_cast<void>(book.add_order(sell(2, 101, 10)));
    CHECK(book.cancel_order(1));
    CHECK(book.ask_quantity_at(100) == 0);
    CHECK(book.best_ask() == std::optional<Price>{101});

    CHECK(book.add_order(buy(3, 101, 10)) ==
          std::vector<Trade>{{3, 2, 101, 10}});
    CHECK(book.best_ask() == std::nullopt);
    CHECK(book.validate_invariants());
}

auto test_order_index_consistency() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 99, 10)));
    static_cast<void>(book.add_order(buy(2, 100, 20)));
    static_cast<void>(book.add_order(sell(3, 102, 30)));
    CHECK(book.validate_invariants());

    CHECK(book.add_order(sell(4, 100, 15)) ==
          std::vector<Trade>{{2, 4, 100, 15}});
    CHECK(book.cancel_order(1));
    CHECK(book.modify_order(3, 103, 25));
    CHECK(book.size() == 2);
    CHECK(book.contains(2));
    CHECK(book.contains(3));
    CHECK(book.validate_invariants());

    book.clear();
    CHECK(book.empty());
    CHECK(book.trade_count() == 0);
    CHECK(book.validate_invariants());
}

auto test_total_quantity_consistency_and_overflow() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 10)));
    static_cast<void>(book.add_order(buy(2, 100, 20)));
    static_cast<void>(book.add_order(buy(3, 100, 30)));
    CHECK(book.bid_quantity_at(100) == 60);

    CHECK(book.add_order(sell(4, 100, 15)) ==
          std::vector<Trade>({
              {1, 4, 100, 10},
              {2, 4, 100, 5},
          }));
    CHECK(book.bid_quantity_at(100) == 45);
    CHECK(book.cancel_order(3));
    CHECK(book.bid_quantity_at(100) == 15);
    CHECK(book.validate_invariants());

    OrderBook overflow;
    const auto maximum = std::numeric_limits<Quantity>::max();
    static_cast<void>(overflow.add_order(buy(10, 100, maximum)));
    CHECK_THROWS_AS(overflow.add_order(buy(11, 100, 1)), std::overflow_error);
    CHECK(overflow.bid_quantity_at(100) == maximum);
    CHECK(!overflow.contains(11));
    CHECK(overflow.validate_invariants());

    OrderBook modify_overflow;
    static_cast<void>(modify_overflow.add_order(buy(20, 99, 1)));
    static_cast<void>(modify_overflow.add_order(buy(21, 100, maximum)));
    CHECK_THROWS_AS(modify_overflow.modify_order(20, 100, 1),
                    std::overflow_error);
    CHECK(modify_overflow.get_order(20) ==
          std::optional<Order>{buy(20, 99, 1)});
    CHECK(modify_overflow.bid_quantity_at(100) == maximum);
    CHECK(modify_overflow.validate_invariants());

    OrderBook ask_overflow;
    static_cast<void>(ask_overflow.add_order(sell(22, 100, maximum)));
    CHECK_THROWS_AS(ask_overflow.add_order(sell(23, 100, 1)),
                    std::overflow_error);
    static_cast<void>(ask_overflow.add_order(sell(24, 101, 1)));
    CHECK_THROWS_AS(ask_overflow.modify_order(24, 100, 1),
                    std::overflow_error);
    CHECK(ask_overflow.get_order(24) ==
          std::optional<Order>{sell(24, 101, 1)});
    CHECK(ask_overflow.ask_quantity_at(100) == maximum);
    CHECK(ask_overflow.validate_invariants());

    OrderBook maximum_fill;
    static_cast<void>(maximum_fill.add_order(sell(30, 100, maximum)));
    CHECK(maximum_fill.add_order(buy(31, 100, maximum)) ==
          std::vector<Trade>{{31, 30, 100, maximum}});
    CHECK(maximum_fill.empty());
    CHECK(maximum_fill.validate_invariants());
}

auto test_bid_fifo_at_same_price() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 10)));
    static_cast<void>(book.add_order(buy(2, 100, 10)));
    static_cast<void>(book.add_order(buy(3, 100, 10)));

    CHECK(book.add_order(sell(4, 100, 15)) ==
          std::vector<Trade>({
              {1, 4, 100, 10},
              {2, 4, 100, 5},
          }));
    CHECK(!book.contains(1));
    CHECK(book.get_order(2) == std::optional<Order>{buy(2, 100, 5)});
    CHECK(book.get_order(3) == std::optional<Order>{buy(3, 100, 10)});
    CHECK(book.bid_quantity_at(100) == 15);
    CHECK(book.validate_invariants());
}

auto test_sell_matches_multiple_price_levels() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 103, 10)));
    static_cast<void>(book.add_order(buy(2, 102, 20)));
    static_cast<void>(book.add_order(buy(3, 101, 30)));

    CHECK(book.add_order(sell(4, 101, 50)) ==
          std::vector<Trade>({
              {1, 4, 103, 10},
              {2, 4, 102, 20},
              {3, 4, 101, 20},
          }));
    CHECK(book.get_order(3) == std::optional<Order>{buy(3, 101, 10)});
    CHECK(book.best_bid() == std::optional<Price>{101});
    CHECK(book.bid_quantity_at(101) == 10);
    CHECK(book.validate_invariants());
}

auto test_modify_price_quantity_and_crossing() -> void {
    OrderBook repriced;
    static_cast<void>(repriced.add_order(buy(1, 100, 10)));
    const auto repriced_result = repriced.modify_order(1, 101, 20);
    CHECK(repriced_result.modified);
    CHECK(repriced_result.trades.empty());
    CHECK(repriced.get_order(1) == std::optional<Order>{buy(1, 101, 20)});
    CHECK(repriced.best_bid() == std::optional<Price>{101});
    CHECK(repriced.validate_invariants());

    OrderBook crossing;
    static_cast<void>(crossing.add_order(sell(10, 100, 10)));
    static_cast<void>(crossing.add_order(buy(11, 99, 20)));
    const auto crossing_result = crossing.modify_order(11, 101, 20);
    CHECK(crossing_result.modified);
    CHECK(crossing_result.trades == std::vector<Trade>{{11, 10, 100, 10}});
    CHECK(!crossing.contains(10));
    CHECK(crossing.get_order(11) == std::optional<Order>{buy(11, 101, 10)});
    CHECK(crossing.trade_count() == 1);
    CHECK(crossing.validate_invariants());
}

auto test_cancel_partially_filled_remainder() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 50)));
    static_cast<void>(book.add_order(buy(2, 100, 20)));
    CHECK(book.ask_quantity_at(100) == 30);
    CHECK(book.cancel_order(1));
    CHECK(book.ask_quantity_at(100) == 0);
    CHECK(book.empty());
    CHECK(book.validate_invariants());
}

auto test_inactive_id_can_be_reused() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(sell(1, 100, 1)));
    static_cast<void>(book.add_order(buy(2, 100, 1)));
    CHECK(book.empty());

    CHECK(book.add_order(buy(1, 99, 2)).empty());
    CHECK(book.get_order(1) == std::optional<Order>{buy(1, 99, 2)});
    CHECK(book.validate_invariants());
}

auto test_incoming_sell_residual_can_be_cancelled() -> void {
    OrderBook book;
    static_cast<void>(book.add_order(buy(1, 100, 20)));

    CHECK(book.add_order(sell(2, 99, 50)) ==
          std::vector<Trade>{{1, 2, 100, 20}});
    CHECK(book.get_order(2) == std::optional<Order>{sell(2, 99, 30)});
    CHECK(book.ask_quantity_at(99) == 30);
    CHECK(book.best_ask() == std::optional<Price>{99});
    CHECK(book.cancel_order(2));
    CHECK(book.empty());
    CHECK(book.validate_invariants());
}

struct TestCase {
    std::string_view name;
    void (*run)();
};

}  // namespace

int main() {
    const std::vector<TestCase> tests{
        {"empty book", test_empty_book},
        {"add single bid and ask", test_add_single_bid_and_ask},
        {"best price ordering", test_best_price_ordering},
        {"non-crossing orders", test_non_crossing_orders},
        {"exact match", test_exact_match},
        {"crossed marketable limits", test_crossed_marketable_limits},
        {"incoming order partial fill", test_incoming_order_partial_fill},
        {"resting order partial fill", test_resting_order_partial_fill},
        {"multiple price-level match", test_multiple_price_level_match},
        {"FIFO at the same price", test_fifo_at_same_price},
        {"cancel order", test_cancel_order},
        {"cancel unknown order", test_cancel_unknown_order},
        {"cancel best prices", test_cancel_best_prices},
        {"modify requeues order", test_modify_requeues_order},
        {"duplicate order id", test_duplicate_order_id},
        {"invalid order fields", test_invalid_order_fields},
        {"price-level removal", test_price_level_removal},
        {"order-index consistency", test_order_index_consistency},
        {"total-quantity consistency and overflow",
         test_total_quantity_consistency_and_overflow},
        {"bid FIFO at the same price", test_bid_fifo_at_same_price},
        {"sell matches multiple price levels",
         test_sell_matches_multiple_price_levels},
        {"modify price, quantity, and crossing",
         test_modify_price_quantity_and_crossing},
        {"cancel partially-filled remainder",
         test_cancel_partially_filled_remainder},
        {"inactive id can be reused", test_inactive_id_can_be_reused},
        {"incoming sell residual can be cancelled",
         test_incoming_sell_residual_can_be_cancelled},
    };

    std::size_t passed{};
    for (const auto& test : tests) {
        try {
            test.run();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    std::cout << passed << '/' << tests.size() << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
