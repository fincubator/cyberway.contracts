#pragma once
#include "test_api_helper.hpp"
#include "../common/config.hpp"
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/int_arithmetic.hpp>
#include <eosio/chain/global_property_object.hpp>

namespace eosio { namespace testing {

struct cyber_bios_api: base_contract_api {
public:
    cyber_bios_api(golos_tester* tester, name code)
    :   base_contract_api(tester, code) {}
    
    variant get_resstate()const {
        auto all = _tester->get_all_chaindb_rows(account_name(), 0, N(resstate), false);
        BOOST_REQUIRE(all.size() == 1);
        return all[0];
    }
    
    std::vector<int64_t> get_pending_usage() {
        return get_resstate()["pending_usage"].as<std::vector<int64_t> >();
    }
    
    std::vector<int64_t> get_virtual_limits() {
        return get_resstate()["virtual_limits"].as<std::vector<int64_t> >();
    }
    
    void set_virtual_limits(const std::vector<uint64_t>& limits) {
        auto& db = _tester->control->chaindb();
        db.modify(*db.find<chain::resource_limits::resource_limits_state_object>(0), [&]( auto& s) {
            s.virtual_limits = limits;
        });
    }
    
    void set_global_usage(const std::vector<int64_t>& u) {
        auto& db = _tester->control->chaindb();
        db.modify(*db.find<chain::resource_limits::resource_limits_state_object>(0), [&]( auto& s) {
            for (size_t i = 0; i < u.size(); i++) {
                auto avg = u[i];
                s.block_usage_accumulators[i].consumed = u[i];
                s.block_usage_accumulators[i].value_ex = u[i] *  chain::config::rate_limiting_precision;
                s.pending_usage[i] = u[i];
            }
        });
    }
    
    std::vector<eosio::chain::resource_limits::ratio> get_pricelist()const {
        auto rlm = _tester->control->get_resource_limits_manager();
        return rlm.get_pricelist();
    }
    
    int64_t get_cost(const std::vector<int64_t>& prev_usage, const std::vector<int64_t>& new_usage)const {
        auto pricelist = get_pricelist();
        
        int64_t ret = 0;
        for (size_t i = 0; i < pricelist.size(); i++) {
            auto add = chain::int_arithmetic::safe_prop_ceil<int64_t>(new_usage[i] - prev_usage[i], pricelist[i].numerator, pricelist[i].denominator);
            if (add > 0) {
                ret += add;
            }
        }
        return ret;
    }
    
    uint64_t get_account_balance(const account_name& account)const {
        auto rlm = _tester->control->get_resource_limits_manager();
        return rlm.get_account_balance(fc::time_point(), account, rlm.get_pricelist(), false);
    }
    
    /*
       std::vector<uint64_t> max_block_usage;
   std::vector<uint64_t> max_transaction_usage;
   
   std::vector<uint64_t> target_virtual_limits;
   std::vector<uint64_t> min_virtual_limits;
   std::vector<uint64_t> max_virtual_limits;
   std::vector<uint32_t> usage_windows;
   
   std::vector<uint16_t> virtual_limit_decrease_pct;
   std::vector<uint16_t> virtual_limit_increase_pct;

   std::vector<uint32_t> account_usage_windows;
   */
   
    const chain::chain_config& get_params() {
        return _tester->control->get_global_properties().configuration;
    }
    
    enum limits_kind { BLOCK, TRANSACTION, TARGET_VIRT, MIN_VIRT, MAX_VIRT };
    action_result set_limits(const std::vector<uint64_t>& arg, limits_kind k) { //numeric_limits<uint64_t>::max() for previous value
        auto params = _tester->control->get_global_properties().configuration;
        auto& prev_limits = k == BLOCK       ? params.max_block_usage       : (
                            k == TRANSACTION ? params.max_transaction_usage : (
                            k == TARGET_VIRT ? params.target_virtual_limits : (
                            k == MIN_VIRT    ? params.min_virtual_limits    :
                                               params.max_virtual_limits)));
        
        if (prev_limits.size () != arg.size()) {
            return base_tester::wasm_assert_msg("incorrect vector size");
        }
        
        for (size_t i = 0; i < arg.size(); i++) {
            if (arg[i] != std::numeric_limits<uint64_t>::max()) {
                prev_limits[i] = arg[i];
            }
        }
        return push(N(setparams), _code, args()("params", params));
    }
    
    action_result set_min_transaction_cpu_usage(uint32_t arg) {
        auto params = _tester->control->get_global_properties().configuration;
        params.min_transaction_cpu_usage = arg;
        return push(N(setparams), _code, args()("params", params));
    }
};

}} // eosio::testing
