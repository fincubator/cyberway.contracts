#include <cyber.bios/cyber.bios.hpp>
#include <cyber.bios/config.hpp>
#include <cyber.govern/cyber.govern.hpp>
#include <cyber.token/cyber.token.hpp>

#include <eosio/system.hpp>

eosio::symbol core_symbol() {
    const static auto sym = cyber::config::system_token;
    return sym;
}

namespace cyber {
    
using namespace eosio;
using namespace eosiosystem;
using namespace cyber::config;

// constants
const uint32_t seconds_per_hour      = 60 * 60;
const uint32_t seconds_per_day       = 24 * 3600;

// config
const uint32_t min_time_from_last_win = seconds_per_day;
const uint32_t min_time_from_last_bid = seconds_per_day;

void bios::onblock(ignore<block_header> header) {
    require_auth(_self);
    
    eosio::block_timestamp timestamp;
    name producer;
    _ds >> timestamp >> producer;
    INLINE_ACTION_SENDER(govern, onblock)(govern_name, {{govern_name, active_name}}, {producer});
    //TODO: update names
}

void bios::checkwin() {
    auto now = eosio::current_time_point();
    auto state = state_singleton(_self, _self.value);
    auto s = state.get_or_create(_self, state_info{now});

    if ((now - s.last_close_bid).to_seconds() > min_time_from_last_win) {
        name_bid_table bids(_self, _self.value);
        auto idx = bids.get_index<"highbid"_n>();
        auto highest = idx.lower_bound( std::numeric_limits<uint64_t>::max()/2 );
        if( highest != idx.end() && highest->high_bid > 0 &&
            (now - highest->last_bid_time).to_seconds() > min_time_from_last_bid) {
            s.last_close_bid = now;
            state.set(s, _self);
            idx.modify( highest, same_payer, [&]( auto& b ){
                b.high_bid = -b.high_bid;
            });
        }
    }
}

// TODO: can be added to CDT
name get_prefix(name acc) {
    auto tmp = acc.value;
    auto remain_bits = 64;
    for (; remain_bits >= -1; remain_bits-=5) {
        if ((tmp >> 59) == 0) { // 1st symbol is dot
            break;
        }
        tmp <<= 5;
    }
    if (remain_bits == -1) {
        remain_bits = 0;
    }
    return name{acc.value >> remain_bits << remain_bits};
}

void bios::bidname( name bidder, name newname, eosio::asset bid ) {
   require_auth( bidder );
   checkwin();

   eosio::check( get_prefix(newname) == newname, "you can only bid on top-level prefix" );

   eosio::check( (bool)newname, "the empty name is not a valid account name to bid on" );
   eosio::check( (newname.value & 0xFull) == 0, "13 character names are not valid account names to bid on" );
   eosio::check( (newname.value & 0x1F0ull) == 0, "accounts with 12 character names can be created without bidding required" );
   eosio::check( !is_account( newname ), "account already exists" );
   eosio::check( bid.symbol == core_symbol(), "asset must be system token" );
   eosio::check( bid.amount > 0, "insufficient bid" );

   INLINE_ACTION_SENDER(eosio::token, transfer)(
      token_name, { {bidder, active_name} },
      { bidder, names_name, bid, std::string("bid name ")+ newname.to_string() }
   );

   name_bid_table bids(_self, _self.value);
   print( name{bidder}, " bid ", bid, " on ", name{newname}, "\n" );
   auto current = bids.find( newname.value );
   if( current == bids.end() ) {
      bids.emplace( bidder, [&]( auto& b ) {
         b.newname = newname;
         b.high_bidder = bidder;
         b.high_bid = bid.amount;
         b.last_bid_time = eosio::current_time_point();
      });
   } else {
      eosio::check( current->high_bid > 0, "this auction has already closed" );
      eosio::check( bid.amount - current->high_bid > (current->high_bid / 10), "must increase bid by 10%" );
      eosio::check( current->high_bidder != bidder, "account is already highest bidder" );

      bid_refund_table refunds_table(_self, _self.value);

      auto it = refunds_table.find( current->high_bidder.value );
      if ( it != refunds_table.end() ) {
         refunds_table.modify( it, same_payer, [&](auto& r) {
               r.amount += asset( current->high_bid, core_symbol() );
            });
      } else {
         refunds_table.emplace( bidder, [&](auto& r) {
               r.bidder = current->high_bidder;
               r.amount = asset( current->high_bid, system_token );
            });
      }

      bids.modify( current, bidder, [&]( auto& b ) {
         b.high_bidder = bidder;
         b.high_bid = bid.amount;
         b.last_bid_time = eosio::current_time_point();
      });
   }
}

void bios::bidrefund( name bidder ) {
   checkwin();
   bid_refund_table refunds_table(_self, _self.value);
   auto it = refunds_table.find( bidder.value );
   eosio::check( it != refunds_table.end(), "Nothing to refund" );
   INLINE_ACTION_SENDER(eosio::token, transfer)(
        token_name, { {names_name, active_name}, {bidder, active_name} },
        { names_name, bidder, asset(it->amount), "refund bid on name" }
   );
   refunds_table.erase( it );
}

void bios::newaccount(name creator, name newact, ignore<authority> owner, ignore<authority> active) {
    if( creator != _self ) {
        if ((newact.value & 0x1F0ull) == 0) { // Name is less than 12 characters
            auto prefix = get_prefix(newact);
            if (prefix == newact) {
                name_bid_table bids(_self, _self.value);
                auto current = bids.require_find( newact.value, "no active bid for name" );
                eosio::check( current->high_bidder == creator, "only highest bidder can claim" );
                eosio::check( current->high_bid < 0, "auction for name is not closed yet" );
                bids.erase( current );
            } else {
                eosio::check( creator == prefix, "only prefix may create this account" );
            }
        }
    }
}

}

EOSIO_DISPATCH( cyber::bios, (newaccount)(setprods)(setparams)(reqauth)(setabi)(setcode)(onblock)(checkwin)(bidname)(bidrefund) )
