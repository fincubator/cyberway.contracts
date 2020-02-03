/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>
#include <vector>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;
   using std::vector;

   class [[eosio::contract("cyber.token")]] token : public contract {
      public:
         using contract::contract;

         [[eosio::action]]
         void create( name   issuer,
                      asset  maximum_supply);

         [[eosio::action]]
         void issue( name to, asset quantity, string memo );

         [[eosio::action]]
         void retire( asset quantity, string memo );

         [[eosio::action]]
         void transfer( name    from,
                        name    to,
                        asset   quantity,
                        string  memo );

         struct recipient {
             name    to;
             asset   quantity;
             string  memo;
         };

         [[eosio::action]]
         void bulktransfer( name from, vector<recipient> recipients );

         [[eosio::action]]
         void payment( name    from,
                       name    to,
                       asset   quantity,
                       string  memo );

         [[eosio::action]]
         void bulkpayment( name from, vector<recipient> recipients );

         [[eosio::action]]
         void claim(name owner , asset quantity);

         [[eosio::action]]
         void open( name owner, const symbol& symbol, name ram_payer );

         [[eosio::action]]
         void close( name owner, const symbol& symbol );

         static asset get_supply( name token_contract_account, symbol_code sym_code )
         {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.supply;
         }
         
         static asset get_max_supply( name token_contract_account, symbol_code sym_code )
         {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.max_supply;
         }

         static asset get_balance( name token_contract_account, name owner, symbol_code sym_code )
         {
            accounts accountstable( token_contract_account, owner.value );
            const auto& ac = accountstable.get( sym_code.raw() );
            return ac.balance;
         }

         static name get_issuer( name token_contract_account, symbol_code sym_code )
         {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.issuer;
         }

         static bool balance_exist( name token_contract_account, name owner, symbol_code sym_code )
         {
            accounts accountstable( token_contract_account, owner.value );
            return accountstable.find( sym_code.raw() ) != accountstable.end();
         }

      private:
         struct account {
            asset    balance;
            asset    payments;

            uint64_t primary_key()const { return balance.symbol.code().raw(); }
         };

         struct [[eosio::event("currency")]] currency_stats {
            asset    supply;
            asset    max_supply;
            name     issuer;

            uint64_t primary_key()const { return supply.symbol.code().raw(); }
         };

         struct [[eosio::event]] balance_event {
            name     account;
            asset    balance;
            asset    payments;
         };

         using accounts [[eosio::order("balance._sym")]] =
            eosio::multi_index<"accounts"_n, account>;
         using stats [[using eosio: order("supply._sym"), scope_type("symbol_code")]] =
            eosio::multi_index<"stat"_n, currency_stats>;

         void sub_balance( name owner, asset value );
         void add_balance( name owner, asset value, name ram_payer );
         void add_payment( name owner, asset value, name ram_payer );

         void send_currency_event(const currency_stats& stat);
         void send_balance_event(name acc, const account& accinfo);

         void do_transfer( name    from,
                           name    to,
                           const asset& quantity,
                           const string& memo,
                           bool payment = false);
   };
} /// namespace eosio
