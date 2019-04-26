#include "golos_tester.hpp"
#include "cyber.token_test_api.hpp"
#include "cyber.stake_test_api.hpp"
#include "cyber.govern_test_api.hpp"
#include "contracts.hpp"
#include "../cyber.stake/include/cyber.stake/config.hpp"
#include "../cyber.bios/include/cyber.bios/config.hpp"
#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/int_arithmetic.hpp>

namespace cfg = cyber::config;
using eosio::chain::config::stake_account_name;
using eosio::chain::config::govern_account_name;
using eosio::chain::config::default_virtual_ram_limit;
using namespace eosio::testing;
using namespace eosio::chain;
using namespace eosio::chain::int_arithmetic;
using namespace fc; 
static const auto _token = cfg::system_token;

class cyber_govern_tester : public golos_tester {
protected:
    cyber_token_api token;
    cyber_stake_api stake;
    cyber_govern_api govern;

    time_point_sec head_block_time() { return time_point_sec(control->head_block_time().time_since_epoch().to_seconds()); };
 
public: 
    cyber_govern_tester()
        : golos_tester(govern_account_name, false)
        , token({this, cfg::token_name, cfg::system_token})
        , stake({this, stake_account_name})
        , govern({this, govern_account_name})
    { 
        create_accounts({_alice, _bob, _carol, _whale,
            cfg::token_name, cfg::worker_name});
        produce_block();
        install_contract(stake_account_name, contracts::stake_wasm(), contracts::stake_abi());
        install_contract(cfg::token_name, contracts::token_wasm(), contracts::token_abi());
        
    }
    
    const account_name _issuer = cfg::issuer_name;
    const account_name _alice = N(alice);
    const account_name _bob = N(bob);
    const account_name _carol = N(carol);
    const account_name _whale = N(whale);
    
    void deploy_sys_contracts(int64_t max_supply_amount = -1) {
        if (max_supply_amount < 0) {
            max_supply_amount = 10000000000;
        }
        BOOST_TEST_MESSAGE("--- creating token and stake"); 
        BOOST_CHECK_EQUAL(success(), token.create(_issuer, asset(max_supply_amount, token._symbol)));
        BOOST_CHECK_EQUAL(success(), stake.create(_issuer, token._symbol, 
            std::vector<uint8_t>{30, 10, 3, 1}, cfg::balances_update_window, 7 * 24 * 60 * 60, 52));
            
        BOOST_TEST_MESSAGE("--- installing governance contract");
        install_contract(govern_account_name, contracts::govern_wasm(), contracts::govern_abi());
        BOOST_TEST_MESSAGE("--- installing bios contract");
        //sys token and stake must already exist at this moment
        install_contract(config::system_account_name, contracts::bios_wasm(), contracts::bios_abi());
        produce_block();
        BOOST_TEST_MESSAGE("    ...done");
    }
    
    int64_t get_emission_per_block(double rate) {
        return (token.get_stats()["supply"].as<asset>().get_amount() * rate) / cfg::blocks_per_year;
    }
    
    const std::map<account_name, int64_t> get_rewards_of_elected(int64_t emission_per_block, uint32_t cur_block, const std::map<account_name, uint32_t>& prod_blocks) {
        uint32_t total_blocks = 0;
        for (auto& prod : prod_blocks) {
            total_blocks += prod.second;
        }
        size_t i = 0;
        int64_t total_reward = ((emission_per_block * (cfg::_100percent - cfg::block_reward_pct) / cfg::_100percent) * total_blocks) * 
            (cfg::_100percent - cfg::workers_reward_pct) / cfg::_100percent;
        std::map<account_name, int64_t> ret;
        for (auto& prod : prod_blocks) {
            ret[prod.first] += total_reward / prod_blocks.size();
            if(cur_block % prod_blocks.size() == i) {
                ret[prod.first] += total_reward % prod_blocks.size();
            }
            i++;
        }
        return ret;
    }
    
    int64_t get_rewards_of_workers(int64_t emission_per_block, uint32_t cur_block, const std::map<account_name, uint32_t>& prod_blocks) {
        uint32_t total_blocks = 0;
        for (auto& prod : prod_blocks) {
            total_blocks += prod.second;
        }
        return ((emission_per_block * (cfg::_100percent - cfg::block_reward_pct) / cfg::_100percent) * total_blocks) * 
            cfg::workers_reward_pct / cfg::_100percent;
    }

    const int64_t get_reward_for_blocks(int64_t emission_per_block, uint32_t blocks) {
        int64_t reward_for_block = (emission_per_block * cfg::block_reward_pct) / cfg::_100percent;
        return blocks * reward_for_block;
    }
    
    void emission_test(int64_t supply_amount, int64_t granted_amount, const std::string& agent_name, double rate, int64_t max_supply_amount = -1) {
        deploy_sys_contracts(max_supply_amount);
        BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _whale, asset(supply_amount, token._symbol), ""));
        stake.register_candidate(_alice, token._symbol.to_symbol_code());
        if (granted_amount) {
            BOOST_CHECK_EQUAL(success(), token.transfer(_whale, stake_account_name, asset(granted_amount, token._symbol), agent_name));
        }
        govern.wait_schedule_activation();
        //cyber is still a producer
        BOOST_CHECK_EQUAL(govern.get_target_emission_per_block(), 0);
        produce_block();
        auto emission = get_emission_per_block(rate);
        BOOST_CHECK_EQUAL(govern.get_target_emission_per_block(), emission);
        BOOST_CHECK_EQUAL(govern.get_producer(_alice)["confirmed_balance"].as<int64_t>(), 
            supply_amount != max_supply_amount ? safe_pct<int64_t>(cfg::block_reward_pct, emission) : 0);
        produce_block();
    }

    struct errors: contract_error_messages {
    } err;
};  

BOOST_AUTO_TEST_SUITE(cyber_govern_tests)
BOOST_FIXTURE_TEST_CASE(rewards_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("Rewards test");
    deploy_sys_contracts();
    int64_t init_amount = 1;
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _whale, asset(cfg::blocks_per_year * 100, token._symbol), ""));
    stake.register_candidate(_bob, token._symbol.to_symbol_code());
    stake.register_candidate(_alice, token._symbol.to_symbol_code());
    BOOST_CHECK_EQUAL(success(), token.transfer(_whale, stake_account_name, asset(init_amount, token._symbol), "alice"));
    BOOST_CHECK_EQUAL(success(), token.transfer(_whale, stake_account_name, asset(init_amount, token._symbol), "bob"));
    produce_block();
    auto lpu = head_block_time();
    BOOST_CHECK_EQUAL(success(), stake.updatefunds(_bob, token._symbol.to_symbol_code()));
    BOOST_CHECK_EQUAL(success(), stake.updatefunds(_alice, token._symbol.to_symbol_code()));
    govern.wait_schedule_activation();
    auto blocks_num = govern.wait_update_schedule_and_reward();
    std::map<account_name, uint32_t> prod_blocks;
    prod_blocks[_alice] = blocks_num / 2;
    prod_blocks[_bob] = blocks_num / 2;
    if (blocks_num % 2) {
        prod_blocks[control->head_block_producer()]++;
    }
    auto emission_per_block = get_emission_per_block(double(cfg::emission_addition + cfg::emission_factor) / cfg::_100percent);
    auto rewards_of_elected = get_rewards_of_elected(emission_per_block, govern.get_block_num(), prod_blocks);
    
    BOOST_CHECK_EQUAL(token.get_account(cfg::worker_name)["balance"].as<asset>().get_amount(), 
        get_rewards_of_workers(emission_per_block, govern.get_block_num(), prod_blocks));
    
    auto last_prod = control->head_block_producer();
    auto prev_prod = last_prod == _alice ? _bob : _alice;
    
    BOOST_CHECK_EQUAL(govern.get_producer(last_prod)["pending_balance"].as<int64_t>(), 0);
    BOOST_CHECK_EQUAL(govern.get_producer(last_prod)["confirmed_balance"].as<int64_t>(), get_reward_for_blocks(emission_per_block, prod_blocks[last_prod]) + rewards_of_elected[last_prod]);
    BOOST_CHECK_EQUAL(govern.get_producer(prev_prod)["pending_balance"].as<int64_t>(), rewards_of_elected[prev_prod]);
    BOOST_CHECK_EQUAL(govern.get_producer(prev_prod)["confirmed_balance"].as<int64_t>(), get_reward_for_blocks(emission_per_block, prod_blocks[prev_prod]));
    
    last_prod = control->head_block_producer();
    prev_prod = last_prod == _alice ? _bob : _alice;
    auto block_before_missed = govern.get_block_num();
    for (auto i = 0; i < 20; i++) {
        if (control->pending_block_state()->header.producer == last_prod) {
            produce_block(fc::microseconds(eosio::chain::config::block_interval_us * 2));
            i++;
        }
        else {
            produce_block();
        }
        prod_blocks[prev_prod]++;
    }
    produce_block();
    
    BOOST_CHECK_EQUAL(govern.get_producer(last_prod)["confirmed_balance"].as<int64_t>(), 
        get_reward_for_blocks(emission_per_block, prod_blocks[last_prod] + 1) + rewards_of_elected[last_prod]);
    BOOST_CHECK_EQUAL(govern.get_producer(prev_prod)["confirmed_balance"].as<int64_t>(), 
        rewards_of_elected[prev_prod] + get_reward_for_blocks(emission_per_block, prod_blocks[prev_prod]));
    
    govern.wait_sum_up(1);
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol)["balance"], init_amount);
    BOOST_CHECK_EQUAL(stake.get_agent(_bob,   token._symbol)["balance"], init_amount);
    auto balance_a = govern.get_producer(_alice)["confirmed_balance"].as<int64_t>();
    auto balance_b = govern.get_producer(_bob)  ["confirmed_balance"].as<int64_t>();
    produce_block();
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol)["balance"], balance_a + init_amount);
    BOOST_CHECK_EQUAL(stake.get_agent(_bob,   token._symbol)["balance"], balance_b + init_amount);
    
    produce_block();
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE(emission)
BOOST_FIXTURE_TEST_CASE(no_staked_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/no_staked_test");
    emission_test(cfg::blocks_per_year * 100, 0, "", double(cfg::emission_addition + cfg::emission_factor) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(no_votes_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/no_votes_test");
    emission_test(cfg::blocks_per_year * 100, cfg::blocks_per_year * 100, "bob", double(cfg::emission_addition + cfg::emission_factor) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(not_enough_votes_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/not_enough_votes_test");
    emission_test(cfg::blocks_per_year * 100, safe_pct<int64_t>(cfg::emission_min_arg / 2, cfg::blocks_per_year * 100), "alice", 
        double(cfg::emission_addition + cfg::emission_factor) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(still_not_enough_votes_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/still_not_enough_votes_test");
    emission_test(cfg::blocks_per_year * 100, safe_pct<int64_t>(cfg::emission_min_arg, cfg::blocks_per_year * 100), "alice", 
        double(cfg::emission_addition + cfg::emission_factor) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(half_supply_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/half_supply_test");
    emission_test(cfg::blocks_per_year * 100, cfg::blocks_per_year * 50, "alice", 
        double(cfg::emission_addition + cfg::emission_factor / 2) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(a_lot_of_votes_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/a_lot_of_votes_test");
    emission_test(cfg::blocks_per_year * 100, safe_pct<int64_t>(cfg::emission_max_arg, cfg::blocks_per_year * 100), "alice", 
        double(cfg::emission_addition) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(all_voted_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/all_voted_test");
    emission_test(cfg::blocks_per_year * 100, cfg::blocks_per_year * 100, "alice", 
        double(cfg::emission_addition) / cfg::_100percent);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(max_supply_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("emission/max_supply_test");
    emission_test(cfg::blocks_per_year * 100, cfg::blocks_per_year * 100, "alice", 
        double(cfg::emission_addition) / cfg::_100percent, cfg::blocks_per_year * 100);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END() // emission

BOOST_AUTO_TEST_SUITE(election)
BOOST_FIXTURE_TEST_CASE(set_producers_test, cyber_govern_tester) try {
    
    BOOST_TEST_MESSAGE("election/set_producers_test");
    deploy_sys_contracts();
    stake.register_candidate(_bob, token._symbol.to_symbol_code());
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group({_bob}));
    stake.register_candidate(_alice, token._symbol.to_symbol_code());
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group({_bob, _alice}));
    
    std::vector<account_name> crowd_of_bps;
    for (int u = cfg::active_producers_num - 2; u >= 0; u--) {
        auto user = user_name(u);
        crowd_of_bps.emplace_back(user);
        create_accounts({user});
        stake.register_candidate(user, token._symbol.to_symbol_code());
        BOOST_CHECK_EQUAL(success(), token.issue(_issuer, user, asset((u + 1) * 1000, token._symbol), ""));
        BOOST_CHECK_EQUAL(success(), token.transfer(user, stake_account_name, asset((u + 1) * 1000, token._symbol)));
    }
    auto crowd_and_alice = crowd_of_bps;
    crowd_and_alice.emplace_back(_alice);
    auto crowd_and_bob = crowd_of_bps;
    crowd_and_bob.emplace_back(_bob);
    
    govern.wait_update_schedule_and_reward(1);
    BOOST_TEST_MESSAGE("--- head_block_producer = " << control->head_block_producer());
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_bob));
   
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _carol, asset(3, token._symbol), ""));
    BOOST_CHECK_EQUAL(success(), token.transfer(_carol, stake_account_name, asset(1, token._symbol)));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_carol, token._symbol.to_symbol_code(), 1));
    
    BOOST_CHECK_EQUAL(success(), stake.delegate(_carol, _bob, asset(1, token._symbol)));
    
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_alice));
    
    BOOST_CHECK_EQUAL(success(), token.transfer(_carol, stake_account_name, asset(2, token._symbol)));
    BOOST_CHECK_EQUAL(success(), stake.delegate(_carol, _alice, asset(2, token._symbol)));
    
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_bob));
    
    stake.register_candidate(_whale, token._symbol.to_symbol_code());
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _whale, asset(100500, token._symbol), ""));
    BOOST_CHECK_EQUAL(success(), token.transfer(_whale, stake_account_name, asset(100500, token._symbol)));
    
    govern.wait_schedule_activation();
    
    crowd_of_bps.back() = _whale;
    crowd_and_alice = crowd_of_bps;
    crowd_and_alice.emplace_back(_alice);
    crowd_and_bob = crowd_of_bps;
    crowd_and_bob.emplace_back(_bob);
    auto crowd_and_user = crowd_of_bps;
    crowd_and_user.emplace_back(user_name(0));
    
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_alice));
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_bob));
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_user));
    
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_alice));
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group(crowd_and_bob));
    produce_block();
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(no_key_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("no_key_test");
    deploy_sys_contracts();
    BOOST_CHECK_EQUAL(success(), stake.open(_alice, token._symbol.to_symbol_code()));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_alice, token._symbol.to_symbol_code(), 0));
    stake.register_candidate(_bob, token._symbol.to_symbol_code());
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group({_bob}));
    
    produce_block();
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(unreg_producer_no_spare_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("unreg_producer_no_spare_test");
    deploy_sys_contracts();
    stake.register_candidate(_alice, token._symbol.to_symbol_code());
    stake.register_candidate(_bob, token._symbol.to_symbol_code());
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group({_bob, _alice}));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_alice, token._symbol.to_symbol_code(), 1));
    govern.wait_schedule_activation(false);
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group({_bob, _alice}));
    stake.register_candidate(_carol, token._symbol.to_symbol_code());
    govern.wait_schedule_activation();
    BOOST_CHECK_EQUAL(govern.get_active_elected_producers(), govern.make_producers_group({_bob, _carol}));
    
    produce_block();
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(unreg_producer_test, cyber_govern_tester) try {
    BOOST_TEST_MESSAGE("unreg_producer_test");
    deploy_sys_contracts();
    
    std::vector<account_name> cur_pbs;
    for (int u = 0; u < cfg::elected_producers_num + 1; u++) {
        auto user = user_name(u);
        create_accounts({user});
        stake.register_candidate(user, token._symbol.to_symbol_code());
        BOOST_CHECK_EQUAL(success(), token.issue(_issuer, user, asset((u + 1) * 1000, token._symbol), ""));
        BOOST_CHECK_EQUAL(success(), token.transfer(user, stake_account_name, asset((u + 1) * 1000, token._symbol)));
        if (u) {
            cur_pbs.emplace_back(user_name(u));
        }
    }
    
    govern.wait_update_schedule_and_reward();
    BOOST_CHECK_EQUAL(govern.get_elected_producers(), govern.make_producers_group(cur_pbs));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(user_name(cfg::elected_producers_num), token._symbol.to_symbol_code(), 1));
    govern.wait_update_schedule_and_reward();
    cur_pbs.back() = user_name(0);
    BOOST_CHECK_EQUAL(govern.get_elected_producers(), govern.make_producers_group(cur_pbs));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(user_name(0), token._symbol.to_symbol_code(), 1));
    govern.wait_update_schedule_and_reward();
    cur_pbs.pop_back();
    BOOST_CHECK_EQUAL(govern.get_elected_producers(), govern.make_producers_group(cur_pbs));
    
    while (cur_pbs.size() > cfg::active_producers_num) {
        BOOST_CHECK_EQUAL(success(), stake.setproxylvl(cur_pbs.back(), token._symbol.to_symbol_code(), 1));
        govern.wait_update_schedule_and_reward();
        cur_pbs.pop_back();
        BOOST_CHECK_EQUAL(govern.get_elected_producers(), govern.make_producers_group(cur_pbs));
    }
    
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(cur_pbs.back(), token._symbol.to_symbol_code(), 1));
    govern.wait_update_schedule_and_reward();
    BOOST_CHECK_EQUAL(govern.get_elected_producers(), govern.make_producers_group(cur_pbs));
    
    produce_block();
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END() // election

BOOST_AUTO_TEST_SUITE_END() // cyber_govern_tests
