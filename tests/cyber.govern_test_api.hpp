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
    variant get_producers_group(bool active = true, bool elected = true)const {
        auto all = _tester->get_all_chaindb_rows(_code, _code.value, N(producer), false);
        std::vector<account_name> ret_vec;
        for(auto& v : all) {
            if (v["elected"].as<bool>() == elected && static_cast<bool>(v["commencement_block"].as<uint32_t>()) == active) {
                ret_vec.emplace_back(v["account"].as<account_name>());
            }
        }
        variant ret;
        to_variant(ret_vec, ret);
        return ret;
    }
    
    variant get_producer(account_name account) {
        return get_struct(_code, N(producer), account.value, "producer_struct");
    }

    uint32_t get_block_num()const {
        auto all = _tester->get_all_chaindb_rows(_code, _code.value, N(governstate), false);
        BOOST_REQUIRE(all.size() == 1);
        return all[0]["block_num"].as<uint32_t>();
    }

    static variant make_producers_group(std::vector<account_name> accounts) {
        std::sort(accounts.begin(), accounts.end());
        variant ret;
        to_variant(accounts, ret);
        return ret;
    }
    uint32_t get_block_offset()const {
         return _tester->control->head_block_num() - get_block_num();
    }
    
    uint32_t wait_update_schedule_and_reward() {
        auto prev_block = _tester->control->head_block_num();
        while(get_block_num() % cyber::config::reward_from_funds_interval != 0) {
            _tester->produce_block();
        }
        uint32_t ret = _tester->control->head_block_num() - prev_block;
        BOOST_TEST_MESSAGE("--- waited " << ret << " blocks to update schedule and reward");
        return ret;
    }
    
    uint32_t wait_schedule_activation() {
        auto prev_version = _tester->control->head_block_header().schedule_version;
        auto prev_block = _tester->control->head_block_num();
        auto prev_block_offset = get_block_offset();
        _tester->wait_irreversible_block(_tester->control->head_block_num());
        _tester->wait_irreversible_block(_tester->control->head_block_num());
        _tester->produce_block();
        BOOST_REQUIRE(_tester->control->head_block_header().schedule_version == prev_version);
        BOOST_REQUIRE(get_block_offset() == prev_block_offset);
        BOOST_REQUIRE(_tester->control->pending_block_state()->active_schedule.version == prev_version + 1);
        uint32_t ret = _tester->control->head_block_num() - prev_block;
        BOOST_TEST_MESSAGE("--- waited " << ret << " blocks for schedule activation");
        return ret;
    }    
};

}} // eosio::testing
