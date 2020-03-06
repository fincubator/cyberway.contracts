#pragma once
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>
#include <eosio/crypto.hpp>
#include <cyber.govern/config.hpp>
#include <eosio/binary_extension.hpp>

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

        // using of binary_extension is a temporary decision only for upgrade phase
        eosio::binary_extension<time_point_sec> last_resize_step;
        eosio::binary_extension<int8_t> resize_shift;
        eosio::binary_extension<uint32_t> schedule_version;

        // this operator is required to set binary extension fields
        void operator = (const state_info& s) {
            last_schedule_increase = s.last_schedule_increase;
            block_num = s.block_num;
            target_emission_per_block = s.target_emission_per_block;
            funds = s.funds;
            last_propose_block_num = s.last_propose_block_num;
            required_producers_num = s.required_producers_num;
            last_producers_num = s.last_producers_num;

            set_extension_field(last_resize_step, s.last_resize_step);
            set_extension_field(resize_shift, s.resize_shift);
            set_extension_field(schedule_version, s.schedule_version);
        }

        template<typename T>
        void set_extension_field(eosio::binary_extension<T>& dst, const eosio::binary_extension<T>& src) const {
            // temporary decision only for upgrade phase
            if (src.has_value()) {
                dst.emplace(src.value());
            } else {
                dst.reset();
            }
        }
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

    struct omission_struct {
        name account;
        uint16_t count;
        uint16_t resets = 0;
        uint64_t primary_key()const { return account.value; }
        uint16_t by_count()const { return count; }
    };
};

    using state_singleton [[eosio::order("id","asc")]] = eosio::singleton<"governstate"_n, structures::state_info>;
    using balances [[eosio::order("account","asc")]] = eosio::multi_index<"balance"_n, structures::balance_struct>;
    using unconfirmed_balances [[eosio::order("account","asc")]] = eosio::multi_index<"uncbalance"_n, structures::balance_struct>;
    using obliged_producers [[eosio::order("account","asc")]] = eosio::multi_index<"obligedprod"_n, structures::producer_struct>;

    using omission_count_index [[using eosio: order("count","desc"), non_unique]] = eosio::indexed_by<"bycount"_n, eosio::const_mem_fun<structures::omission_struct, uint16_t, &structures::omission_struct::by_count> >;
    using omissions [[eosio::order("account","asc")]] = eosio::multi_index<"omission"_n, structures::omission_struct, omission_count_index>;
    
    void promote_producers();
    void propose_producers(structures::state_info& s);
    void reward_producers(balances& balances_table, structures::state_info& s);
    void reward_workers(structures::state_info& s);
    int64_t get_target_emission_per_block(int64_t supply) const;
    
public:
    using contract::contract;
    [[eosio::action]] void onblock(name producer, eosio::binary_extension<uint32_t> schedule_version);
    [[eosio::action]] void setshift(int8_t shift);
};

} /// cyber
