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

#include <array>
#include <boost/asio/io_context.hpp>

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int LOT_SIZE = 10;
constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEARST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) /
                                    TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK =
    MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

constexpr long VOLUME = 20;
constexpr ulong K = 5;

AutoTrader::AutoTrader(boost::asio::io_context &context)
    : BaseAutoTrader(context) {}

void AutoTrader::DisconnectHandler() {
  BaseAutoTrader::DisconnectHandler();
  RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId,
                                     const std::string &errorMessage) {
  RLOG(LG_AT, LogLevel::LL_INFO)
      << "error with order " << clientOrderId << ": " << errorMessage;
  if (clientOrderId != 0 && (!mOrders.empty())) {
    OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
  }
}

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {
  RLOG(LG_AT, LogLevel::LL_INFO)
      << "hedge order " << clientOrderId << " filled for " << volume
      << " lots at $" << price << " average price in cents";

  // Find the hedge
  auto it = mHedges.find(clientOrderId);
  if (it != mHedges.end()) {
    // Get elements
    Side side = std::get<0>(it->second);
    ulong og_price = std::get<1>(it->second);
    ulong og_volume = std::get<2>(it->second);
    bool unhedge = std::get<3>(it->second);

    if (price == 0 && volume == 0) {
      RLOG(LG_AT, LogLevel::LL_INFO)
          << "hedge order " << clientOrderId << " UNSUCCESSFUL";

      if (side == Side::BUY) {
        mHedges[mNextMessageId] =
            std::make_tuple(Side::SELL, MAX_ASK_NEAREST_TICK, VOLUME, unhedge);

        SendHedgeOrder(mNextMessageId++, Side::SELL, MAX_ASK_NEAREST_TICK,
                       VOLUME);
        RLOG(LG_AT, LogLevel::LL_INFO)
            << "New hedge order " << clientOrderId << " sent for "
            << ": Side " << side << ": Price " << MAX_ASK_NEAREST_TICK;
      } else {
        mHedges[mNextMessageId] =
            std::make_tuple(Side::BUY, MIN_BID_NEARST_TICK, VOLUME, unhedge);

        SendHedgeOrder(mNextMessageId++, Side::BUY, MIN_BID_NEARST_TICK,
                       VOLUME);
        RLOG(LG_AT, LogLevel::LL_INFO)
            << "New hedge order " << clientOrderId << " sent for "
            << ": Side " << side << ": Price " << MIN_BID_NEARST_TICK;
      }
      mHedges.erase(clientOrderId);
      return;
    }

    // Update FUTURE position
    if (side == Side::BUY)
      mFUTPosition += volume;
    else if (side == Side::SELL)
      mFUTPosition -= volume;

    if (volume == og_volume) {
    } else {
      // Decrement og_volume
      // Ignore for now
    }

    if (unhedge) {
      Side unhedgeSide = (side == Side::BUY) ? Side::SELL : Side::BUY;

      // The side is hedge side so you actually ask for opposite
      long unhedge_price = (unhedgeSide == Side::BUY) ? curETFBidPriceBook[0]
                                                      : curETFAskPriceBook[0];

      mOrders[mNextMessageId] =
          std::make_tuple(unhedgeSide, unhedge_price, VOLUME, false);
      SendInsertOrder(mNextMessageId++, unhedgeSide, unhedge_price, VOLUME,
                      Lifespan::FILL_AND_KILL);
    }

    mHedges.erase(clientOrderId);
    RLOG(LG_AT, LogLevel::LL_INFO)
        << "Ticks " << mTicks << ": Hedge Side " << std::get<0>(it->second)
        << ": Hedge Original Price " << std::get<1>(it->second)
        << ": Hedge Original Volume " << std::get<2>(it->second)
        << ": Hedged for Volume " << volume << ": Hedged for Price " << price
        << ": mETFPosition: " << mETFPosition << ": Reverse " << unhedge
        << ": mFUTPosition: " << mFUTPosition;
  } else
    RLOG(LG_AT, LogLevel::LL_INFO)
        << "Hedge order " << clientOrderId << " Not found!";
}

void AutoTrader::OrderBookMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &bidVolumes) {
  if (instrument == Instrument::FUTURE) {
    // Update futures order book
    curFUTAskPriceBook = std::move(askPrices);
    curFUTAskVolBook = std::move(askVolumes);
    curFUTBidPriceBook = std::move(bidPrices);
    curFUTBidVolBook = std::move(bidVolumes);
  } else if (instrument == Instrument::ETF) {
    // Update ETF order book
    curETFAskPriceBook = std::move(askPrices);
    curETFAskVolBook = std::move(askVolumes);
    curETFBidPriceBook = std::move(bidPrices);
    curETFBidVolBook = std::move(bidVolumes);
  }

  long buyVolume = 0, sellVolume = 0;
  long Pt, Vol;
  if (instrument == Instrument::FUTURE) {
    ulong bestBid = bidPrices[0];
    ulong bestAsk = askPrices[0];

    // Get volumes
    for (int i = 0; i < (int)TOP_LEVEL_COUNT; ++i) {
      buyVolume += bidPrices[i];
      sellVolume += askPrices[i];
    }

    // Get directions
    bool fluctuationBuySide =
        ((buyVolume - sellVolume) / (buyVolume + sellVolume)) > 0.5;
    bool fluctuationSellSide =
        ((sellVolume - buyVolume) / (buyVolume + sellVolume)) > 0.5;

    // Check active order types
    clearAllOrders();

    if (mOrders.size() > 2)
      return;

    if (mETFPosition <= POSITION_LIMIT) {
      // BUY SIDE
      Pt = bestBid;
      if (fluctuationBuySide && !fluctuationSellSide) {
        // Ignore
      } else if (!fluctuationBuySide && fluctuationSellSide)
        Pt -= TICK_SIZE_IN_CENTS * 2;
      else
        Pt -= TICK_SIZE_IN_CENTS;

      if (bidVolETF > 0) {
        mOrders[mNextMessageId] =
            std::make_tuple(Side::BUY, Pt, bidVolETF, true);
        SendInsertOrder(mNextMessageId++, Side::BUY, Pt, bidVolETF,
                        Lifespan::GOOD_FOR_DAY);
        RLOG(LG_AT, LogLevel::LL_INFO)
            << "Order side BUY placed no. " << mNextMessageId - 1 << " For "
            << Pt << " Vol " << bidVolETF;
      }

      if (mETFPosition <= (long)POSITION_LIMIT / 2) {
        mOrders[mNextMessageId] = std::make_tuple(Side::BUY, Pt, 25, true);
        SendInsertOrder(mNextMessageId++, Side::BUY, bestBid, 25,
                        Lifespan::FILL_AND_KILL);

        RLOG(LG_AT, LogLevel::LL_INFO)
            << "Order side BUY placed no. " << mNextMessageId - 1 << " For "
            << Pt << " Vol " << bidVolETF;
      }
    }

    if (mETFPosition >= -POSITION_LIMIT) {
      // SELL SIDE
      Pt = bestAsk;
      if (fluctuationBuySide && !fluctuationSellSide)
        Pt += TICK_SIZE_IN_CENTS * 2;
      else if (!fluctuationBuySide && fluctuationSellSide) {
        // Ignore
      } else
        Pt += TICK_SIZE_IN_CENTS;

      if (askVolETF > 0) {
        mOrders[mNextMessageId] =
            std::make_tuple(Side::SELL, Pt, askVolETF, true);
        SendInsertOrder(mNextMessageId++, Side::SELL, Pt, askVolETF,
                        Lifespan::GOOD_FOR_DAY);
        RLOG(LG_AT, LogLevel::LL_INFO)
            << "Order SELL side placed no. " << mNextMessageId - 1 << " For "
            << Pt << " Vol " << askVolETF;
      }
    }

    bidETF = bestBid;
    askETF = bestAsk;

  } else {
    // ETF
  }

  RLOG(LG_AT, LogLevel::LL_INFO)
      << "Ticks" << mTicks << "Position: " << mETFPosition
      << " Future Position : " << mFUTPosition << ": order book received for "
      << instrument << " instrument"
      << ": ask prices: " << askPrices[0] << ", " << askPrices[1] << ", "
      << askPrices[2] << ", " << askPrices[3] << ", " << askPrices[4]
      << ": ask volumes: " << askVolumes[0] << ", " << askVolumes[1] << ", "
      << askVolumes[2] << ", " << askVolumes[3] << ", " << askVolumes[4]
      << ": bid prices: " << bidPrices[0] << ", " << bidPrices[1] << ", "
      << bidPrices[2] << ", " << bidPrices[3] << ", " << bidPrices[4]
      << ": bid volumes: " << bidVolumes[0] << ", " << bidVolumes[1] << ", "
      << bidVolumes[2] << ", " << bidVolumes[3] << ", " << bidVolumes[4]
      << ": bidETF " << bidETF << " askETF " << askETF << "Order count "
      << ": mETFPosition: " << mETFPosition << ": Reverse "
      << ": mFUTPosition: " << mFUTPosition << " Orders: " << mOrders.size()
      << ": Buy Volume " << buyVolume << " : Sell Volume " << sellVolume
      << " : Volume traded BId " << bidVolETF << " : Volume traded ask "
      << askVolETF;

  ++mTicks;
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume) {
  RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " filled for "
                                 << volume << " lots at $" << price << " cents";

  // Get order from tracked mOrders
  auto order_it = mOrders.find(clientOrderId);
  if (order_it != mOrders.end()) {
    long orderPrice = std::get<1>(order_it->second);
    long orderVolume = std::get<2>(order_it->second);
    bool notfromhedge = std::get<3>(order_it->second);

    // Bid or Ask
    Side hedgeSide =
        (std::get<0>(order_it->second) == Side::BUY) ? Side::SELL : Side::BUY;

    // The side is hedge side so you actually ask for opposite
    long nearest_tick =
        (hedgeSide == Side::BUY) ? MAX_ASK_NEAREST_TICK : MIN_BID_NEARST_TICK;

    // Update Position
    if (hedgeSide == Side::SELL)
      mETFPosition += volume;
    else
      mETFPosition -= volume;

    // Update volume
    std::get<2>(order_it->second) -= volume;

    // Update the vols
    askVolETF = (long)(POSITION_LIMIT / 2);
    if (mETFPosition >= POSITION_LIMIT) {
      askVolETF = POSITION_LIMIT;
    } else if (mETFPosition <= -POSITION_LIMIT) {
      askVolETF = 0;
    } else if (mETFPosition < 0 && mETFPosition <= -POSITION_LIMIT / 2) {
      askVolETF = POSITION_LIMIT + mETFPosition;
    }

    bidVolETF = (long)(POSITION_LIMIT / 2);
    if (mETFPosition <= -POSITION_LIMIT) {
      bidVolETF = POSITION_LIMIT;
    } else if (mETFPosition >= POSITION_LIMIT) {
      bidVolETF = 0;
    } else if (mETFPosition > 0 && mETFPosition >= POSITION_LIMIT / 2) {
      bidVolETF = POSITION_LIMIT - mETFPosition;
    }

    // Re-adjust orders
    clearAllOrdersBySide(std::get<0>(order_it->second));

    // Hedge order
    mHedges[mNextMessageId] = std::make_tuple(hedgeSide, price, volume, false);
    SendHedgeOrder(mNextMessageId++, hedgeSide, nearest_tick, volume);

    RLOG(LG_AT, LogLevel::LL_INFO)
        << "!!order " << clientOrderId << " has volume " << volume
        << " priced at" << price << " on side " << std::get<0>(order_it->second)
        << ": Position: " << mETFPosition << ": ASKVOL " << askVolETF
        << ": BIDVOL " << bidVolETF;

  } else
    RLOG(LG_AT, LogLevel::LL_INFO)
        << "order " << clientOrderId << " not found!";
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees) {
  if (remainingVolume == 0) {
    mOrders.erase(clientOrderId);
    RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " erased!"
                                   << " : Num orders " << mOrders.size();
  }
}

void AutoTrader::TradeTicksMessageHandler(
    Instrument instrument, unsigned long sequenceNumber,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &askPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &askVolumes,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &bidPrices,
    const std::array<unsigned long, TOP_LEVEL_COUNT> &bidVolumes) {
  RLOG(LG_AT, LogLevel::LL_INFO)
      << "trade ticks received for " << instrument << " instrument"
      << ": ask prices: " << askPrices[0] << "; ask volumes: " << askVolumes[0]
      << "; bid prices: " << bidPrices[0] << "; bid volumes: " << bidVolumes[0]
      << "; bidETF: " << bidETF << " askETF: " << askETF << "; Recently filled "
      << mOrderRecentlyFilled;
}
