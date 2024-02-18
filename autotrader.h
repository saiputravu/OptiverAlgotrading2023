// Copyright 2021 Optiver Asia Pacific Pty. Ltd.
//
// This file is part of Ready Trader Go.
//
//     Ready Trader Go is free software: you can redistribute it and/or
//     modify it under the terms of the GNU Affero General Public License
//     as published by the Free Software Foundation, either version 3 of
//     the License, or (at your option) any later version.
//
//     Ready Trader Go is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU Affero General Public License for more details.
//
//     You should have received a copy of the GNU Affero General Public
//     License along with Ready Trader Go.  If not, see
//     <https://www.gnu.org/licenses/>.
#ifndef CPPREADY_TRADER_GO_AUTOTRADER_H
#define CPPREADY_TRADER_GO_AUTOTRADER_H

#include <ready_trader_go/baseautotrader.h>
#include <ready_trader_go/types.h>

#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/circular_buffer.hpp>
#include <sstream>
#include <string>
#include <unordered_map>

#include "ready_trader_go/logging.h"

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

// Operator overloads

static constexpr ReadyTraderGo::Side operator!(
    const ReadyTraderGo::Side &side) {
  switch (side) {
    case ReadyTraderGo::Side::BUY:
      return ReadyTraderGo::Side::SELL;
    case ReadyTraderGo::Side::SELL:
      return ReadyTraderGo::Side::BUY;
  }
}

// ------------------

class Utilities {
 public:
  static inline std::string InstrumentToString(
      ReadyTraderGo::Instrument instrument) {
    switch (instrument) {
      case ReadyTraderGo::Instrument::FUTURE:
        return "future";
      case ReadyTraderGo::Instrument::ETF:
        return "etf";
      default:
        return "unknown";
    }
  }

  static inline std::string SideToString(ReadyTraderGo::Side side) {
    switch (side) {
      case ReadyTraderGo::Side::BUY:
        return "buy";
      case ReadyTraderGo::Side::SELL:
        return "sell";
      default:
        return "unknown";
    }
  }

  static inline std::string LifespanToString(ReadyTraderGo::Lifespan lifespan) {
    switch (lifespan) {
      case ReadyTraderGo::Lifespan::GOOD_FOR_DAY:
        return "good_for_day";
      case ReadyTraderGo::Lifespan::FILL_AND_KILL:
        return "fill_and_kill";
      default:
        return "unknown";
    }
  }
};

struct OrderInformation {
  unsigned long tick;  // Tick it was recorded at
  unsigned long id;
  ReadyTraderGo::Side side;
  unsigned long price;
  unsigned long volume;
  ReadyTraderGo::Lifespan lifespan;
  ReadyTraderGo::Instrument instrument;

  inline static std::string ToString(const OrderInformation &order) {
    std::stringstream ss;
    ss << "(Order "
       << "(tick " << order.tick << ") "
       << "(id " << order.id << ") "
       << "(side " << Utilities::SideToString(order.side) << ") "
       << "(price " << order.price << ") "
       << "(volume " << order.volume << ") "
       << "(volume " << Utilities::LifespanToString(order.lifespan) << ")"
       << ")";

    return ss.str();
  }
};

class AutoTrader : public ReadyTraderGo::BaseAutoTrader {
 public:
  explicit AutoTrader(boost::asio::io_context &context);

  // Called when the execution connection is lost.
  void DisconnectHandler() override;

  // Called when the matching engine detects an error.
  // If the error pertains to a particular order, then the client_order_id
  // will identify that order, otherwise the client_order_id will be zero.
  void ErrorMessageHandler(unsigned long clientOrderId,
                           const std::string &errorMessage) override;

  // Called when one of your hedge orders is filled, partially or fully.
  //
  // The price is the average price at which the order was (partially) filled,
  // which may be better than the order's limit price. The volume is
  // the number of lots filled at that price.
  //
  // If the order was unsuccessful, both the price and volume will be zero.
  void HedgeFilledMessageHandler(unsigned long clientOrderId,
                                 unsigned long price,
                                 unsigned long volume) override;

  // Called periodically to report the status of an order book.
  // The sequence number can be used to detect missed or out-of-order
  // messages. The five best available ask (i.e. sell) and bid (i.e. buy)
  // prices are reported along with the volume available at each of those
  // price levels.
  void OrderBookMessageHandler(
      ReadyTraderGo::Instrument instrument, unsigned long sequenceNumber,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &askPrices,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &askVolumes,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &bidPrices,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &bidVolumes) override;

  // Called when one of your orders is filled, partially or fully.
  void OrderFilledMessageHandler(unsigned long clientOrderId,
                                 unsigned long price,
                                 unsigned long volume) override;

  // Called when the status of one of your orders changes.
  // The fill volume is the number of lots already traded, remaining volume
  // is the number of lots yet to be traded and fees is the total fees paid
  // or received for this order.
  // Remaining volume will be set to zero if the order is cancelled.
  void OrderStatusMessageHandler(unsigned long clientOrderId,
                                 unsigned long fillVolume,
                                 unsigned long remainingVolume,
                                 signed long fees) override;

  // Called periodically when there is trading activity on the market.
  // The five best ask (i.e. sell) and bid (i.e. buy) prices at which there
  // has been trading activity are reported along with the aggregated volume
  // traded at each of those price levels.
  // If there are less than five prices on a side, then zeros will appear at
  // the endd of both the prices and volumes arrays.
  void TradeTicksMessageHandler(
      ReadyTraderGo::Instrument instrument, unsigned long sequenceNumber,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &askPrices,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &askVolumes,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &bidPrices,
      const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
          &bidVolumes) override;

  // Send a hedge order
  void SendHedgeOrder(unsigned long clientOrderId, ReadyTraderGo::Side side,
                      unsigned long price, unsigned long volume) override;

  // Send a hedge order without needing to track id
  inline void SendHedgeOrder(ReadyTraderGo::Side side, unsigned long price,
                             unsigned long volume) {
    SendHedgeOrder(++mOrderId, side, price, volume);
  }

  // Send an hedge order given a OrderInformation struct
  inline void SendHedgeOrder(OrderInformation &order) {
    SendHedgeOrder(order.side, order.price, order.volume);
  }

  // Send an insert order
  void SendInsertOrder(unsigned long clientOrderId, ReadyTraderGo::Side side,
                       unsigned long price, unsigned long volume,
                       ReadyTraderGo::Lifespan lifespan) override;

  // Send an insert order without needing to track id
  inline void SendInsertOrder(ReadyTraderGo::Side side, unsigned long price,
                              unsigned long volume,
                              ReadyTraderGo::Lifespan lifespan) {
    // Increment orderid and then send
    // This is tracked internally so no need to worry about multiple trackers
    SendInsertOrder(++mOrderId, side, price, volume, lifespan);
  }

  // Send an insert order given a OrderInformation struct
  inline void SendInsertOrder(OrderInformation order) {
    SendInsertOrder(order.side, order.price, order.volume, order.lifespan);
  }

  // Send an amend order on the volume of the order
  inline void SendAmendOrder(unsigned long clientOrderId,
                             unsigned long volume) override;

  // Send an amend order on the price or volume of the order. Keep 0 to not
  // amend By default this actually cancels the order and creates a new one with
  // the price and volume changed
  inline unsigned long SendAmendOrderExtended(unsigned long clientOrderId,
                                              unsigned long price = 0,
                                              unsigned long volume = 0);

  inline void SendCancelOrder(unsigned long clientOrderId) override;

 private:
  // Ticks since start
  ulong mTicks = 0;

  // client order, just tracking one order
  ulong mOrderId = 1;
  std::unordered_map<ulong, OrderInformation> mOrderBook;

  // Position trackers
  long mETFPosition = 0;
  long mFUTPosition = 0;
};

#endif  // CPPREADY_TRADER_GO_AUTOTRADER_H
