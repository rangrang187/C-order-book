#include "order_book.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace orderbook {

namespace {

auto checked_add(Quantity lhs, Quantity rhs) -> Quantity {
    if (rhs > std::numeric_limits<Quantity>::max() - lhs) {
        throw std::overflow_error("price level quantity overflow");
    }
    return lhs + rhs;
}

}  // namespace

auto OrderBook::add_order(Order order) -> std::vector<Trade> {
    validate_order(order);

    if (contains(order.id)) {
        throw std::invalid_argument("duplicate active order id");
    }

    auto trades = order.side == Side::Buy ? match_buy(order) : match_sell(order);
    if (order.quantity > 0) {
        insert_resting_order(std::move(order));
    }

#ifndef NDEBUG
    assert(validate_invariants());
#endif

    return trades;
}

auto OrderBook::cancel_order(OrderId id) -> bool {
    if (!contains(id)) {
        return false;
    }

    erase_order(id);

#ifndef NDEBUG
    assert(validate_invariants());
#endif

    return true;
}

auto OrderBook::modify_order(OrderId id, Price new_price, Quantity new_quantity)
    -> ModifyResult {
    const auto location = orders_.find(id);
    if (location == orders_.end()) {
        return {};
    }

    const auto side = location->second.side;
    const auto original = *location->second.iterator;
    validate_order(Order{id, side, new_price, new_quantity});

    Quantity target_total{};
    if (side == Side::Buy) {
        const auto target = bids_.find(new_price);
        if (target != bids_.end()) {
            target_total = target->second.total_quantity;
        }
    } else {
        const auto target = asks_.find(new_price);
        if (target != asks_.end()) {
            target_total = target->second.total_quantity;
        }
    }

    if (original.price == new_price) {
        assert(target_total >= original.quantity);
        target_total -= original.quantity;
    }
    static_cast<void>(checked_add(target_total, new_quantity));

    erase_order(id);
    auto trades = add_order(Order{id, side, new_price, new_quantity});

#ifndef NDEBUG
    assert(validate_invariants());
#endif

    return ModifyResult{true, std::move(trades)};
}

auto OrderBook::best_bid() const noexcept -> std::optional<Price> {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

auto OrderBook::best_ask() const noexcept -> std::optional<Price> {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

auto OrderBook::contains(OrderId id) const -> bool {
    return orders_.contains(id);
}

auto OrderBook::get_order(OrderId id) const -> std::optional<Order> {
    const auto location = orders_.find(id);
    if (location == orders_.end()) {
        return std::nullopt;
    }
    return *location->second.iterator;
}

auto OrderBook::bid_quantity_at(Price price) const noexcept -> Quantity {
    const auto level = bids_.find(price);
    return level == bids_.end() ? Quantity{} : level->second.total_quantity;
}

auto OrderBook::ask_quantity_at(Price price) const noexcept -> Quantity {
    const auto level = asks_.find(price);
    return level == asks_.end() ? Quantity{} : level->second.total_quantity;
}

auto OrderBook::size() const noexcept -> std::size_t {
    return orders_.size();
}

auto OrderBook::empty() const noexcept -> bool {
    return orders_.empty();
}

auto OrderBook::trade_count() const noexcept -> std::uint64_t {
    return trade_count_;
}

auto OrderBook::validate_invariants() const -> bool {
    std::unordered_set<OrderId> seen;
    seen.reserve(orders_.size());

    const auto validate_levels = [this, &seen](const auto& levels, Side expected_side) {
        for (const auto& [price, level] : levels) {
            if (price <= 0 || level.price != price || level.orders.empty()) {
                return false;
            }

            Quantity calculated_quantity{};
            for (auto order = level.orders.begin(); order != level.orders.end(); ++order) {
                if (order->id == 0 || order->side != expected_side || order->price != price ||
                    order->quantity == 0) {
                    return false;
                }
                if (order->quantity >
                    std::numeric_limits<Quantity>::max() - calculated_quantity) {
                    return false;
                }
                calculated_quantity += order->quantity;

                if (!seen.emplace(order->id).second) {
                    return false;
                }

                const auto indexed = orders_.find(order->id);
                if (indexed == orders_.end() || indexed->second.side != expected_side ||
                    indexed->second.price != price || indexed->second.iterator != order) {
                    return false;
                }
            }

            if (calculated_quantity != level.total_quantity) {
                return false;
            }
        }
        return true;
    };

    if (!validate_levels(bids_, Side::Buy) || !validate_levels(asks_, Side::Sell)) {
        return false;
    }
    if (seen.size() != orders_.size()) {
        return false;
    }
    if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first) {
        return false;
    }
    return true;
}

auto OrderBook::clear() noexcept -> void {
    bids_.clear();
    asks_.clear();
    orders_.clear();
    trade_count_ = 0;
}

auto OrderBook::validate_order(const Order& order) -> void {
    if (order.id == 0) {
        throw std::invalid_argument("order id must be non-zero");
    }
    if (order.price <= 0) {
        throw std::invalid_argument("order price must be positive");
    }
    if (order.quantity == 0) {
        throw std::invalid_argument("order quantity must be positive");
    }
    if (order.side != Side::Buy && order.side != Side::Sell) {
        throw std::invalid_argument("invalid order side");
    }
}

auto OrderBook::match_buy(Order& incoming) -> std::vector<Trade> {
    std::vector<Trade> trades;

    while (incoming.quantity > 0 && !asks_.empty() &&
           incoming.price >= asks_.begin()->first) {
        auto level = asks_.begin();
        auto& price_level = level->second;

        while (incoming.quantity > 0 && !price_level.orders.empty()) {
            auto& resting = price_level.orders.front();
            const auto traded_quantity = std::min(incoming.quantity, resting.quantity);

            trades.push_back(
                Trade{incoming.id, resting.id, resting.price, traded_quantity});
            ++trade_count_;

            incoming.quantity -= traded_quantity;
            resting.quantity -= traded_quantity;
            assert(price_level.total_quantity >= traded_quantity);
            price_level.total_quantity -= traded_quantity;

            if (resting.quantity == 0) {
                orders_.erase(resting.id);
                price_level.orders.pop_front();
            }
        }

        if (price_level.orders.empty()) {
            assert(price_level.total_quantity == 0);
            asks_.erase(level);
        }
    }

    return trades;
}

auto OrderBook::match_sell(Order& incoming) -> std::vector<Trade> {
    std::vector<Trade> trades;

    while (incoming.quantity > 0 && !bids_.empty() &&
           incoming.price <= bids_.begin()->first) {
        auto level = bids_.begin();
        auto& price_level = level->second;

        while (incoming.quantity > 0 && !price_level.orders.empty()) {
            auto& resting = price_level.orders.front();
            const auto traded_quantity = std::min(incoming.quantity, resting.quantity);

            trades.push_back(
                Trade{resting.id, incoming.id, resting.price, traded_quantity});
            ++trade_count_;

            incoming.quantity -= traded_quantity;
            resting.quantity -= traded_quantity;
            assert(price_level.total_quantity >= traded_quantity);
            price_level.total_quantity -= traded_quantity;

            if (resting.quantity == 0) {
                orders_.erase(resting.id);
                price_level.orders.pop_front();
            }
        }

        if (price_level.orders.empty()) {
            assert(price_level.total_quantity == 0);
            bids_.erase(level);
        }
    }

    return trades;
}

auto OrderBook::insert_resting_order(Order order) -> void {
    const auto insert = [this](auto& levels, Order resting) {
        auto [level, inserted] = levels.try_emplace(
            resting.price, PriceLevel{resting.price, Quantity{}, OrderList{}});
        auto& price_level = level->second;

        try {
            const auto new_total =
                checked_add(price_level.total_quantity, resting.quantity);
            price_level.orders.push_back(std::move(resting));
            price_level.total_quantity = new_total;
        } catch (...) {
            if (inserted && price_level.orders.empty()) {
                levels.erase(level);
            }
            throw;
        }

        auto order = std::prev(price_level.orders.end());
        try {
            const auto insertion = orders_.emplace(
                order->id, OrderLocation{order->side, order->price, order});
            if (!insertion.second) {
                throw std::logic_error("order index insertion failed");
            }
        } catch (...) {
            price_level.total_quantity -= order->quantity;
            price_level.orders.erase(order);
            if (price_level.orders.empty()) {
                levels.erase(level);
            }
            throw;
        }
    };

    if (order.side == Side::Buy) {
        insert(bids_, std::move(order));
    } else {
        insert(asks_, std::move(order));
    }
}

auto OrderBook::erase_order(OrderId id) -> void {
    const auto indexed = orders_.find(id);
    assert(indexed != orders_.end());

    const auto erase_from = [this, indexed](auto& levels) {
        const auto level = levels.find(indexed->second.price);
        assert(level != levels.end());

        auto& price_level = level->second;
        const auto order = indexed->second.iterator;
        assert(price_level.total_quantity >= order->quantity);
        price_level.total_quantity -= order->quantity;
        price_level.orders.erase(order);
        orders_.erase(indexed);

        if (price_level.orders.empty()) {
            assert(price_level.total_quantity == 0);
            levels.erase(level);
        }
    };

    if (indexed->second.side == Side::Buy) {
        erase_from(bids_);
    } else {
        erase_from(asks_);
    }
}

}  // namespace orderbook
