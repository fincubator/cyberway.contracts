#pragma once
#include <eosio/action.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

namespace cyber {
   using eosio::permission_level;
   using eosio::public_key;
   using eosio::ignore;
   using eosio::name;
   using eosio::time_point_sec;
   using eosio::contract;
   using eosio::asset;

   struct permission_level_weight {
      permission_level  permission;
      uint16_t          weight;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( permission_level_weight, (permission)(weight) )
   };

   struct key_weight {
      eosio::public_key  key;
      uint16_t           weight;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( key_weight, (key)(weight) )
   };

   struct wait_weight {
      uint32_t           wait_sec;
      uint16_t           weight;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( wait_weight, (wait_sec)(weight) )
   };

   struct authority {
      uint32_t                              threshold = 0;
      std::vector<key_weight>               keys;
      std::vector<permission_level_weight>  accounts;
      std::vector<wait_weight>              waits;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( authority, (threshold)(keys)(accounts)(waits) )
   };
   
    struct block_header {
        uint32_t                                  timestamp;
        name                                      producer;
        uint16_t                                  confirmed = 0;
        eosio::checksum256                        previous;
        eosio::checksum256                        transaction_mroot;
        eosio::checksum256                        action_mroot;
        uint32_t                                  schedule_version = 0;
        std::optional<eosio::producer_schedule>   new_producers;
   };

   class [[eosio::contract("cyber.bios")]] bios : public contract {
      struct state_info {
          time_point_sec last_close_bid;
      };
      using state_singleton [[eosio::order("id","asc")]] =
        eosio::singleton<"biosstate"_n, state_info>;

      struct bid_refund {
        name         bidder;
        eosio::asset amount;

        uint64_t primary_key()const { return bidder.value; }
      };
      using bid_refund_table [[eosio::order("bidder","asc")]] =
        eosio::multi_index<"bidrefunds"_n, bid_refund>;

      struct name_bid {
        name              newname;
        name              high_bidder;
        int64_t           high_bid = 0; ///< negative high_bid == closed auction waiting to be claimed
        eosio::time_point_sec last_bid_time;

        uint64_t primary_key()const { return newname.value;                    }
        uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
      };
      using by_high [[using eosio: order("high_bid","desc"), non_unique]] =
        eosio::indexed_by<"highbid"_n, eosio::const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid>>;
      using name_bid_table [[eosio::order("newname","asc")]] =
        eosio::multi_index<"namebids"_n, name_bid, by_high>;

         void check_stake(name account);
      public:
         using contract::contract;
         [[eosio::action]]
         void newaccount( name             creator,
                          name             name,
                          ignore<authority> owner,
                          ignore<authority> active);


         [[eosio::action]]
         void updateauth(  ignore<name>  account,
                           ignore<name>  permission,
                           ignore<name>  parent,
                           ignore<authority> auth ) {}

         [[eosio::action]]
         void deleteauth( ignore<name>  account,
                          ignore<name>  permission ) {}

         [[eosio::action]]
         void linkauth(  ignore<name>    account,
                         ignore<name>    code,
                         ignore<name>    type,
                         ignore<name>    requirement  ) {}

         [[eosio::action]]
         void unlinkauth( ignore<name>  account,
                          ignore<name>  code,
                          ignore<name>  type ) {}

         [[eosio::action]]
         void canceldelay( ignore<permission_level> canceling_auth, ignore<eosio::checksum256> trx_id ) {}

         [[eosio::action]]
         void onerror( ignore<uint128_t> sender_id, ignore<std::vector<char>> sent_trx ) {}

         [[eosio::action]]
         void setcode( name account, uint8_t vmtype, uint8_t vmversion, const std::vector<char>& code ) {}

         [[eosio::action]]
         void setprods( std::vector<eosio::producer_key> schedule ) {
            require_auth( _self );
            eosio::set_proposed_producers( schedule );
         }

         [[eosio::action]]
         void setparams( const eosio::blockchain_parameters& params ) {
            require_auth( _self );
            eosio::set_blockchain_parameters( params );
         }

         [[eosio::action]]
         void reqauth( name from ) {
            require_auth( from );
         }

         [[eosio::action]]
         void setabi( name account, const std::vector<char>& abi ) {}

         [[eosio::action]]
         void checkwin();

         [[eosio::action]]
         void bidname( name bidder, name newname, eosio::asset bid );

         [[eosio::action]]
         void bidrefund( name bidder );

         [[eosio::action]] void onblock(ignore<block_header> header);
         
         void on_stake_withdraw(name account, asset quantity);
         void on_stake_provide(name provider_name, name consumer_name, asset quantity);

   };

} /// namespace cyber
