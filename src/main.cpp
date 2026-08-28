#include "order_book.h"

#include <iostream>

int main() {
    using orderbook::Order;
    using orderbook::OrderBook;
    using orderbook::Side;

    OrderBook book;
    static_cast<void>(book.add_order(Order{1, Side::Sell, 101, 10}));
    const auto trades = book.add_order(Order{2, Side::Buy, 101, 10});

    for (const auto& trade : trades) {
        std::cout << "trade buy=" << trade.buy_order_id
                  << " sell=" << trade.sell_order_id
                  << " price=" << trade.price
                  << " quantity=" << trade.quantity << '\n';
    }

    return book.empty() && trades.size() == 1 ? 0 : 1;
}
