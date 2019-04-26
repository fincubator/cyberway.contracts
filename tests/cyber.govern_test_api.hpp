#pragma once
#include "test_api_helper.hpp"
#include "../common/config.hpp"
#include "../cyber.govern/include/cyber.govern/config.hpp"

namespace eosio { namespace testing {

struct cyber_govern_api: base_contract_api {
public:
    cyber_govern_api(golos_tester* tester, name code)
    :   base_contract_api(tester, code){}

     ////tables
    template<typename Lambda>
    variant get_producers_group(Lambda cond)const {
        auto all = _tester->get_all_chaindb_rows(_code, _code.value, N(producer), false);
        std::vector<account_name> ret_vec;
        for(auto& v : all) {
            if (cond(static_cast<bool>(v["commencement_block"].as<uint32_t>()), v["votes"].as<int64_t>() >= 0)) {
                ret_vec.emplace_back(v["account"].as<account_name>());
            }
        }
        variant ret;
        to_variant(ret_vec, ret);
        return ret;
    }
    
    variant get_active_elected_producers() const {
        return get_producers_group([](bool active, bool elected) { return active && elected; });
    }
    
    variant get_elected_producers() const {
        return get_producers_group([](bool active, bool elected) { return elected; });
    }
    
    variant get_producer(account_name account) {
        return get_struct(_code, N(producer), account.value, "producer_struct");
    }
    
    variant get_state()const {
        auto all = _tester->get_all_chaindb_rows(_code, _code.value, N(governstate), false);
        BOOST_REQUIRE(all.size() == 1);
        return all[0];
    }

    uint32_t get_block_num()const { return get_state()["block_num"].as<uint32_t>(); }
    int64_t get_target_emission_per_block()const { return get_state()["target_emission_per_block"].as<int64_t>(); }

    static variant make_producers_group(std::vector<account_name> accounts) {
        std::sort(accounts.begin(), accounts.end());
        variant ret;
        to_variant(accounts, ret);
        return ret;
    }
    uint32_t get_block_offset()const {
         return _tester->control->head_block_num() - get_block_num();
    }
    
    uint32_t wait_for_proper_block(uint32_t interval, const std::string& s, uint32_t displ = 0) {
        auto prev_block = _tester->control->head_block_num();
        while((get_block_num() + displ) % interval != 0) {
            _tester->produce_block();
        }
        uint32_t ret = _tester->control->head_block_num() - prev_block;
        BOOST_TEST_MESSAGE("--- waited " << ret << " blocks for " << s << " (displ = " << displ << ")");
        return ret;
    }
    
    uint32_t wait_update_schedule_and_reward(uint32_t displ = 0) {
        auto prev_elected = get_elected_producers();
        auto ret = wait_for_proper_block(cyber::config::reward_from_funds_interval, "update schedule and reward", std::max(uint32_t(1), displ));
        BOOST_REQUIRE(prev_elected == get_elected_producers());
        if (!displ) {
            _tester->produce_block();
            ++ret;
        }
        return ret;
    }
    
    uint32_t wait_sum_up(uint32_t displ = 0) {
        return wait_for_proper_block(cyber::config::sum_up_interval, "sum up", displ);
    }
    
    signed_block_ptr wait_irreversible_block(const uint32_t lib, const std::set<account_name>* allowed_producers = nullptr) {
        signed_block_ptr b;
        while (_tester->control->head_block_state()->dpos_irreversible_blocknum < lib) {
            b = _tester->produce_block();
            BOOST_REQUIRE(!allowed_producers || 
                allowed_producers->find(b->producer) != allowed_producers->end() || 
                b->producer == cyber::config::internal_name);
        }
        return b;
    }
    
    uint32_t wait_schedule_activation(bool change_version = true, bool restr = true) {
        BOOST_REQUIRE(!restr || !_tester->control->proposed_producers().valid());
        auto blocks_for_update = wait_update_schedule_and_reward();
        auto prev_version = _tester->control->head_block_header().schedule_version;
        auto prev_block = _tester->control->head_block_num();
        auto prev_block_offset = get_block_offset();
        auto proposed_schedule_block_num = _tester->control->head_block_num() + 1; // see controller.cpp set_proposed_producers
        
        std::set<account_name> active_producers;
        if (restr) {
            auto active_producers_vec = get_producers_group([](bool active, bool elected) { return active; }).as<std::vector<account_name> >();
            active_producers = std::set<account_name>(active_producers_vec.begin(), active_producers_vec.end());
        }
        
        wait_irreversible_block(proposed_schedule_block_num, restr ? &active_producers : nullptr);
        wait_irreversible_block(_tester->control->head_block_num(), restr ? &active_producers : nullptr);
        BOOST_REQUIRE(_tester->control->head_block_header().schedule_version == prev_version);
        BOOST_REQUIRE(get_block_offset() == prev_block_offset);
        BOOST_REQUIRE(_tester->control->pending_block_state()->active_schedule.version == prev_version + static_cast<int>(change_version));
        auto ret = _tester->control->head_block_num() - prev_block;
        BOOST_TEST_MESSAGE("--- waited " << ret << " more blocks for schedule activation");
        ret += blocks_for_update;
        return ret;
    }    
};

}} // eosio::testing
