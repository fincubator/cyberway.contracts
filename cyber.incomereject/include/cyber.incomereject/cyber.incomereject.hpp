/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <cyber.token/cyber.token.hpp>
#include <common/dispatchers.hpp>

namespace eosio {

   class [[eosio::contract("cyber.incomereject")]] incomereject : public contract {
      public:
         using contract::contract;

         ON_TRANSFER(CYBER_TOKEN, on_transfer) void on_transfer(name from, name to, asset quantity, std::string memo);
   };

} /// namespace eosio
