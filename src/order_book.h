#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace orderbook {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;

enum class Side : std::uint8_t {
    Buy,
    Sell,
};

struct Order {
    OrderId id{};
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};

    friend auto operator==(const Order&, const Order&) -> bool = default;
};

struct Trade {
    OrderId buy_order_id{};
    OrderId sell_order_id{};
    Price price{};
    Quantity quantity{};

    friend auto operator==(const Trade&, const Trade&) -> bool = default;
};

struct ModifyResult {
    bool modified{};
    std::vector<Trade> trades{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return modified;
    }
};

class OrderBook {
public:
    OrderBook() = default;
    ~OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    auto operator=(const OrderBook&) -> OrderBook& = delete;
    OrderBook(OrderBook&&) = delete;
    auto operator=(OrderBook&&) -> OrderBook& = delete;

    [[nodiscard]] auto add_order(Order order) -> std::vector<Trade>;

    auto cancel_order(OrderId id) -> bool;

    [[nodiscard]] auto modify_order(OrderId id, Price new_price,
                                    Quantity new_quantity) -> ModifyResult;

    [[nodiscard]] auto best_bid() const noexcept -> std::optional<Price>;
    [[nodiscard]] auto best_ask() const noexcept -> std::optional<Price>;
    [[nodiscard]] auto contains(OrderId id) const -> bool;
    [[nodiscard]] auto get_order(OrderId id) const -> std::optional<Order>;
    [[nodiscard]] auto bid_quantity_at(Price price) const noexcept -> Quantity;
    [[nodiscard]] auto ask_quantity_at(Price price) const noexcept -> Quantity;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto trade_count() const noexcept -> std::uint64_t;

    // Intended for tests and debug assertions. It verifies both the order index
    // and every price level's aggregate quantity against the actual orders.
    [[nodiscard]] auto validate_invariants() const -> bool;

    auto clear() noexcept -> void;

private:
    using OrderList = std::list<Order>;

    struct PriceLevel {
        Price price{};
        Quantity total_quantity{};
        OrderList orders{};
    };

    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel>;

    struct OrderLocation {
        Side side{};
        Price price{};
        OrderList::iterator iterator{};
    };

    static auto validate_order(const Order& order) -> void;
    [[nodiscard]] auto match_buy(Order& incoming) -> std::vector<Trade>;
    [[nodiscard]] auto match_sell(Order& incoming) -> std::vector<Trade>;
    auto insert_resting_order(Order order) -> void;
    auto erase_order(OrderId id) -> void;

    BidLevels bids_{};
    AskLevels asks_{};
    std::unordered_map<OrderId, OrderLocation> orders_{};
    std::uint64_t trade_count_{};
};

}  // namespace orderbook
