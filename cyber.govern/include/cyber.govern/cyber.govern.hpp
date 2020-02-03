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
    struct state_info {
        time_point_sec last_schedule_increase;
        uint32_t block_num = 0;
        int64_t target_emission_per_block = 0;
        int64_t funds = 0;
        uint32_t last_propose_block_num = 0;
        uint16_t required_producers_num = config::min_producers_num;
        uint16_t last_producers_num = 1;
    };
    
    struct [[using eosio: event("burnreward"), contract("cyber.govern")]] balance_struct {
        name account;
        int64_t amount;
        uint64_t primary_key()const { return account.value; }
    };
    
    struct producer_struct {
        name account;
        uint64_t primary_key()const { return account.value; }
    };
    
    struct pending_producers_info {
        std::vector<name> accounts;
    };
};
    using state_singleton [[eosio::order("id","asc")]] = eosio::singleton<"governstate"_n, structures::state_info>;
    using balances [[eosio::order("account","asc")]] = eosio::multi_index<"balance"_n, structures::balance_struct>;
    using unconfirmed_balances [[eosio::order("account","asc")]] = eosio::multi_index<"uncbalance"_n, structures::balance_struct>;
    using obliged_producers [[eosio::order("account","asc")]] = eosio::multi_index<"obligedprod"_n, structures::producer_struct>;
    using pending_producers [[eosio::order("id","asc")]] = eosio::singleton<"pendingprods"_n, structures::pending_producers_info>;
    
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
