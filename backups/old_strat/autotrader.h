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

#include <math.h>
#include <ready_trader_go/baseautotrader.h>
#include <ready_trader_go/types.h>

#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/circular_buffer.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

  float CalculateSMA(boost::circular_buffer<float> &sma) {
    if (sma.size() <= 0)
      return 0.0;
    long sum = 0;
    for (auto &el : sma)
      sum += el;

    return sum / sma.size();
  }

  void clearAllOrdersBySide(ReadyTraderGo::Side side, bool immutable = false) {
    // Delete from mOrders if sides matches
    // for (auto it = mOrders.begin(); it != mOrders.end();) {
    //     if (std::get<0>(it->second) == side && std::get<3>(it->second)) {
    //         SendCancelOrder(it->first);
    //         mOrders.erase(it++);
    //     } else
    //         ++it;
    // }
    for (auto &[id, tup] : mOrders)
      SendCancelOrder(id);
  }

  void clearAllOrders(bool immutable = false) {
    for (auto &[id, tup] : mOrders)
      SendCancelOrder(id);
  }

private:
  // Ticks since start
  bool mInitialised = false;
  ulong mTicks = 0;

  // Internal tracking for bid and ask for ETF
  ulong bidETF, askETF;
  ulong bidVolETF = 50, askVolETF = 50;

  // Position trackers
  long mETFPosition = 0;
  long mFUTPosition = 0;

  // Track when internal bid and ask is updated
  bool bidUpdated = false, askUpdated = false;

  // Prices
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curETFAskPriceBook;
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curFUTAskPriceBook;
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curETFBidPriceBook;
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curFUTBidPriceBook;

  // Volumes
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curETFAskVolBook;
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curFUTAskVolBook;
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curETFBidVolBook;
  std::array<ulong, ReadyTraderGo::TOP_LEVEL_COUNT> curFUTBidVolBook;

  ulong mOrderRecentlyFilled = 0;
  ulong mNextMessageId = 1;
  boost::circular_buffer<float> sma_ask{26};
  boost::circular_buffer<float> sma_bid{26};

  // Id : (Side, price, vol, cancellable)
  //  Orders (Bid // Ask)
  std::unordered_map<
      unsigned long,
      std::tuple<ReadyTraderGo::Side, unsigned long, unsigned long, bool>>
      mOrders;
  //  Hedges (Buy // Sell)
  std::unordered_map<
      unsigned long,
      std::tuple<ReadyTraderGo::Side, unsigned long, unsigned long, bool>>
      mHedges;
};

#endif // CPPREADY_TRADER_GO_AUTOTRADER_H
