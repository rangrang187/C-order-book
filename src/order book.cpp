class OrderBook {
public:
    auto add_order(const Order& order) -> void;

    auto cancel_order(OrderId order_id) -> bool;

    auto best_bid() const -> std::optional<Price>;

    auto best_ask() const -> std::optional<Price>;

    auto bid_quantity_at(Price price) const -> Quantity;

    auto ask_quantity_at(Price price) const -> Quantity;

    auto empty() const -> bool;

    auto clear() -> void;

private:
    using OrderList = std::list<Order>;

    struct PriceLevel {
        Price price;
        Quantity total_quantity;
        OrderList orders;
    };

    using BidLevels =
        std::map<Price, PriceLevel, std::greater<Price>>;

    using AskLevels =
        std::map<Price, PriceLevel, std::less<Price>>;

    struct OrderLocation {
        Side side;
        Price price;
        OrderList::iterator iterator;
    };

    auto match_buy(Order& incoming) -> void;

    auto match_sell(Order& incoming) -> void;

    auto add_resting_order(Order order) -> void;

    auto remove_empty_bid_level(Price price) -> void;

    auto remove_empty_ask_level(Price price) -> void;

    BidLevels bids_;

    AskLevels asks_;

    std::unordered_map<OrderId, OrderLocation> order_index_;
};