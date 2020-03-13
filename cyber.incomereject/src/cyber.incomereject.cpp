/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */

#include <cyber.incomereject/cyber.incomereject.hpp>

namespace eosio {

void incomereject::on_transfer(name from, name to, asset quantity, std::string memo) {
    eosio::check(_self != to, "Incoming funds rejected");
}

} /// namespace eosio
