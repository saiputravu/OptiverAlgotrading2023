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
#include "autotrader.h"

#include <ready_trader_go/logging.h>
#include <sys/types.h>

#include <array>
#include <boost/asio/io_context.hpp>

#include "ready_trader_go/baseautotrader.h"
#include "ready_trader_go/types.h"

using namespace ReadyTraderGo;

constexpr int LOT_SIZE = 10;
constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEARST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) /
                                    TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK =
    MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

AutoTrader::AutoTrader(boost::asio::io_context& context)
    : BaseAutoTrader(context) {}

void AutoTrader::DisconnectHandler() {
  BaseAutoTrader::DisconnectHandler();
  RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId,
                                     const std::string& errorMessage) {
  auto it = mOrderBook.find(clientOrderId);
  if (it != mOrderBook.end()) {
    // Found order
    RLOG(LG_AT, LogLevel::LL_ERROR)
        << "[ErrorMessageHandler] " << OrderInformation::ToString(it->second)
        << "(Error " << errorMessage << " )";
  } else {
    // Unfound order
    RLOG(LG_AT, LogLevel::LL_ERROR)
        << "[ErrorMessageHandler] "
        << "(Order \"Error finding order" << clientOrderId << "\")"
        << "(Error " << errorMessage << " )";
  }
}

/*** Overriden Order Senders ***/

void AutoTrader::SendInsertOrder(unsigned long clientOrderId,
                                 ReadyTraderGo::Side side, unsigned long price,
                                 unsigned long volume,
                                 ReadyTraderGo::Lifespan lifespan) {
  if (price == 0 || volume == 0) return;

  RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_INFO)
      << "[SendInsertOrder] "
      << "(clientOrderId " << clientOrderId << ")"
      << "(side " << Utilities::SideToString(side) << ")"
      << "(price " << price << ")"
      << "(volume " << volume << ")"
      << "(lifespan " << Utilities::LifespanToString(lifespan) << ")";

  // Record order
  mOrderBook[clientOrderId] = {mTicks, clientOrderId, side,           price,
                               volume, lifespan,      Instrument::ETF};

  // Call super
  BaseAutoTrader::SendInsertOrder(clientOrderId, side, price, volume, lifespan);
}

void AutoTrader::SendHedgeOrder(unsigned long clientOrderId,
                                ReadyTraderGo::Side side, unsigned long price,
                                unsigned long volume) {
  RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_INFO)
      << "[SendHedgeOrder] "
      << "(clientOrderId " << clientOrderId << ")"
      << "(side " << Utilities::SideToString(side) << ")"
      << "(price " << price << ")"
      << "(volume " << volume << ")";

  // Record order (lifespan here does not matter for hedge orders)
  mOrderBook[clientOrderId] = {mTicks,
                               clientOrderId,
                               side,
                               price,
                               volume,
                               Lifespan::GOOD_FOR_DAY,
                               Instrument::FUTURE};

  BaseAutoTrader::SendHedgeOrder(clientOrderId, side, price, volume);
}

inline void AutoTrader::SendAmendOrder(unsigned long clientOrderId,
                                       unsigned long volume) {
  RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_INFO)
      << "[SendAmendOrder] "
      << "(clientOrderId " << clientOrderId << ")"
      << "(volume " << volume << ")";

  auto it = mOrderBook.find(clientOrderId);
  if (it != mOrderBook.end()) {
    BaseAutoTrader::SendAmendOrder(clientOrderId, volume);
    // Update
    it->second.volume = volume;
  } else {
    RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_ERROR)
        << "[SendAmendOrder] "
        << "(clientOrderId " << clientOrderId << " not found)";
  }
}

inline unsigned long AutoTrader::SendAmendOrderExtended(
    unsigned long clientOrderId, unsigned long price, unsigned long volume) {
  RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_INFO)
      << "[SendAmendOrderExtended] "
      << "(clientOrderId " << clientOrderId << ")"
      << "(price " << price << ")"
      << "(volume " << volume << ")";

  auto it = mOrderBook.find(clientOrderId);
  if (it != mOrderBook.end()) {
    // Create a new order information struct, with same information
    OrderInformation order(it->second);

    // Override values
    if (volume) {
      order.volume = volume;
    }
    if (price) {
      order.price = price;
    }

    // Cancel old order
    SendCancelOrder(clientOrderId);

    // Send insert order with updated information
    // This insert should work as we would have some sensible value in order
    // already
    SendInsertOrder(order);
    return mOrderId;
  } else {
    RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_ERROR)
        << "[SendAmendOrder] "
        << "(clientOrderId " << clientOrderId << " not found)";
    return -1;
  }
}

inline void AutoTrader::SendCancelOrder(unsigned long clientOrderId) {
  RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_INFO)
      << "[SendCancelOrder] "
      << "(clientOrderId " << clientOrderId << ")";

  auto it = mOrderBook.find(clientOrderId);
  if (it != mOrderBook.end()) {
    // Clear record of order
    mOrderBook.erase(it);

    // Send cancel order
    BaseAutoTrader::SendCancelOrder(clientOrderId);

  } else {
    RLOG(LG_AT, ReadyTraderGo::LogLevel::LL_ERROR)
        << "[SendCancelOrder] "
        << "(clientOrderId " << clientOrderId << " not found)";
  }
}

/*** ----------------------- ***/

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {
  RLOG(LG_AT, LogLevel::LL_INFO) << "[HedgeFilledMessageHandler] "
                                 << "(clientOrderId " << clientOrderId << ")"
                                 << "(price " << price << ")"
                                 << "(volume " << volume << ")";

  auto it = mOrderBook.find(clientOrderId);
  if (it == mOrderBook.end()) {
    // Order not found
    RLOG(LG_AT, LogLevel::LL_ERROR)
        << "[HedgeFilledMessageHandler] "
        << "(clientOrderId " << clientOrderId << " not found)";
    return;
  }

  auto& order = it->second;

  if (!price && !volume) {
    // unsuccessful hedge
    // just re-do the hedge
    RLOG(LG_AT, LogLevel::LL_ERROR) << "[HedgeFilledMessageHandler] "
                                    << "(unsuccessful hedge, redoing) "
                                    << OrderInformation::ToString(order);

    if (order.side == Side::BUY) {
      order.price += TICK_SIZE_IN_CENTS;
    }
    else {
      order.price -= TICK_SIZE_IN_CENTS;
    }
    SendHedgeOrder(order);
  } else {
    // Succesful hedge, handle partial
    // Once fully clear, remove from internal order book
    order.volume -= volume;
    if (order.volume == 0) {
      RLOG(LG_AT, LogLevel::LL_INFO)
          << "[HedgeFilledMessageHandler] "
          << "(Order fully filled, clearing from internal order book)";
      mOrderBook.erase(it);
    }
  }
}

void AutoTrader::OrderBookMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {
  // Log the message handler
  std::stringstream ss;
  for (ulong i = 0; i < TOP_LEVEL_COUNT; ++i) {
    ss << "[ Bid:(" << bidPrices[i] << "," << bidVolumes[i] << ")"
       << "| Ask:(" << askPrices[i] << "," << askVolumes[i] << ") ]";
  }

  RLOG(LG_AT, LogLevel::LL_INFO)
      << "[OrderBookMessageHandler] "
      << " (ticks " << mTicks << ") "
      << " (seq " << sequenceNumber << ") "
      << Utilities::InstrumentToString(instrument) << " " << ss.str();

  // Stay top of the book
  ulong bestBid = bidPrices[0];
  ulong bestAsk = askPrices[0];
  ulong bestPrice;

  if (instrument == Instrument::FUTURE) {
    return;
  }

  // Get orderbook keys
  ulong instrumentOrders = 0;
  std::vector<ulong> orderIds(mOrderBook.size());
  for (auto [id, order] : mOrderBook) {
    orderIds.push_back(id);

    // Count how many orders are of this instrument we are in
    if (order.instrument == instrument) {
      ++instrumentOrders;
    }
  }

  // Re-price all orders on the book, currently in the book
  for (auto& id : orderIds) {
    // (id, order) iterator
    if (mOrderBook[id].instrument == instrument) {
      // Only edit price if this is the right instrument
      bestPrice = mOrderBook[id].side == Side::BUY ? bestBid : bestAsk;
      SendAmendOrderExtended(id, bestPrice);
    }
  }

  if (instrumentOrders == 0) {
    // If no orders on book, create 2
    SendInsertOrder(Side::BUY, bestBid, 10, Lifespan::GOOD_FOR_DAY);
    SendInsertOrder(Side::SELL, bestAsk, 10, Lifespan::GOOD_FOR_DAY);
  } else if (instrumentOrders == 1) {
    // If there is one order on the book
    // Re-price it and insert opposite side
    auto& order = mOrderBook.begin()->second;
    bestPrice = (!order.side) == Side::BUY ? bestBid : bestAsk;
    SendInsertOrder(!order.side, bestPrice, 10, Lifespan::GOOD_FOR_DAY);
  }

  ++mTicks;
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {
  RLOG(LG_AT, LogLevel::LL_INFO) << "[OrderFilledMessageHandler] "
                                 << "(clientOrderId " << clientOrderId << ") "
                                 << "(price " << price << ") "
                                 << "(volume " << volume << ") ";

  // If order was filled, hedge it and update internal tracker
  auto it = mOrderBook.find(clientOrderId);
  if (it == mOrderBook.end()) {
    ErrorMessageHandler(
        clientOrderId, "OrderFilledMessageHandler called, but order not found");
    return;
  }
  OrderInformation& order = it->second;

  RLOG(LG_AT, LogLevel::LL_INFO) << "[OrderFilledMessageHandler] More Info: "
                                 << OrderInformation::ToString(order);

  // Update order information
  order.volume -= volume;
  if (order.volume == 0) {
    RLOG(LG_AT, LogLevel::LL_INFO)
        << "[OrderFilledMessageHandler] "
        << "(Order fully filled, clearing from internal order book)";
    mOrderBook.erase(it);
  }

  if (order.instrument != Instrument::FUTURE) {
    // Hedge the order in the opposite side
    // TODO hedge with a better price
    //      right now you get (fill price - og price) * volume
    SendHedgeOrder(!order.side, order.price, volume);
  }
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees) {
  RLOG(LG_AT, LogLevel::LL_INFO)
      << "[OrderStatusMessageHandler] "
      << "(clientOrderId " << clientOrderId << ")"
      << "(fillVolume " << fillVolume << ")"
      << "(remainingVolume " << remainingVolume << ")"
      << "(fees " << fees << ")";
}

void AutoTrader::TradeTicksMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes) {
  // Log the message handler
  std::stringstream ss;
  for (ulong i = 0; i < TOP_LEVEL_COUNT; ++i) {
    ss << "[ Bid:(" << bidPrices[i] << "," << bidVolumes[i] << ")"
       << "| Ask:(" << askPrices[i] << "," << askVolumes[i] << ") ]";
  }

  RLOG(LG_AT, LogLevel::LL_INFO)
      << "[TradeTicksMessageHandler] "
      << " (ticks " << mTicks << ") "
      << " (seq " << sequenceNumber << ") "
      << Utilities::InstrumentToString(instrument) << " " << ss.str();
}
