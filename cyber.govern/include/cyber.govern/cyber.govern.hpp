#pragma once
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>
#include <eosio/crypto.hpp>
#include <cyber.govern/config.hpp>

namespace cyber {
using eosio::name;
using eosio::contract;
using eosio::public_key;
using eosio::time_point_sec;

class [[eosio::contract("cyber.govern")]] govern : public contract {

struct structures {
    struct [[eosio::table("state")]] state_info {
        time_point_sec last_schedule_increase;
        uint32_t block_num = 0;
        int64_t target_emission_per_block = 0;
        int64_t funds = 0;
        uint32_t last_propose_block_num = 0;
        uint16_t required_producers_num = config::min_producers_num;
        uint16_t last_producers_num = 1;
    };
    
    struct [[eosio::table]] balance {
        name account;
        int64_t amount;
        uint64_t primary_key()const { return account.value; }
    };
    
    struct [[eosio::table]] producer {
        name account;
        uint64_t primary_key()const { return account.value; }
    };
    
    struct [[eosio::table]] pending_producers_state {
        std::vector<name> accounts;
    };
};
    using state_singleton = eosio::singleton<"governstate"_n, structures::state_info>;
    using balances = eosio::multi_index<"balance"_n, structures::balance>;
    using unconfirmed_balances = eosio::multi_index<"uncbalance"_n, structures::balance>;
    using obliged_producers = eosio::multi_index<"obligedprod"_n, structures::producer>;
    using pending_producers = eosio::singleton<"pendingprods"_n, structures::pending_producers_state>;
    
    void maybe_promote_producers();
    void propose_producers(structures::state_info& s);
    void reward_producers(balances& balances_table, structures::state_info& s);
    void reward_workers(structures::state_info& s);
    int64_t get_target_emission_per_block(int64_t supply) const;
    
public:
    using contract::contract;
    [[eosio::action]] void onblock(name producer);
    [[eosio::action]] void setactprods(std::vector<name> pending_active_producers);
};

} /// cyber
