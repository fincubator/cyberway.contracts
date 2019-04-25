#include <cyber.govern/cyber.govern.hpp>
#include <eosiolib/privileged.hpp> 
#include <cyber.stake/cyber.stake.hpp>
#include <cyber.token/cyber.token.hpp>
#include <cyber.govern/config.hpp>
#include <common/util.hpp>

using namespace cyber::config;

namespace cyber {

void govern::setignored(std::vector<name> ignored_producers) {
    require_auth(_self);
    auto state = state_singleton(_self, _self.value);
    auto s = state.get();
    s.ignored_producers = ignored_producers;
    state.set(s, _self);
}

void govern::onblock(name producer) {
    require_auth(_self);
    
    auto state = state_singleton(_self, _self.value);
    auto s = state.get_or_default(structures::state_info { .block_num = 0, .last_producers_num = 1 });
    
    if (std::find(s.ignored_producers.begin(), s.ignored_producers.end(), producer) != s.ignored_producers.end()) {
        return;
    }
    s.block_num++;
    
    int64_t block_reward = 0;
    if (producer != config::internal_name) {
        auto supply     = eosio::token::get_supply    (config::token_name, system_token.code()).amount;
        auto max_supply = eosio::token::get_max_supply(config::token_name, system_token.code()).amount;
        eosio_assert(max_supply >= supply, "SYSTEM: incorrect supply");
        
        if ((s.block_num % config::update_emission_per_block_interval == 0) || !s.target_emission_per_block) {
            s.target_emission_per_block = get_target_emission_per_block(supply);
        }
        auto cur_block_emission = std::min(max_supply - supply, s.target_emission_per_block);
        block_reward = safe_pct(cur_block_emission, config::block_reward_pct);
        s.funds += cur_block_emission - block_reward;
    }
        
    producers producers_table(_self, _self.value);
    if (s.block_num % config::sum_up_interval == 0) {
        sum_up(producers_table);
    }
    if (s.block_num % config::reward_from_funds_interval == 0) {
        update_and_reward_producers(producers_table, s);
        reward_workers(s);
    }
    
    if (maybe_promote_active_producers(s)) {
        s.active_producers_num = s.pending_active_producers.size();
        s.pending_active_producers.clear();
    }
    
    if (producer != config::internal_name) {
        auto prod_itr = producers_table.find(producer.value);
        eosio_assert(prod_itr != producers_table.end(), "SYSTEM: producer does not exist");
        producers_table.modify(prod_itr, name(), [&](auto& a) {
            a.confirmed_balance += a.pending_balance + block_reward;
            a.pending_balance = 0;
            a.last_block_produced = s.block_num;
        });
    }

    state.set(s, _self);
}

govern::change_of_participants govern::get_change_of_producers(producers& producers_table, std::vector<name> new_producers, bool active_only) {
    std::sort(new_producers.begin(), new_producers.end());
    std::vector<name> old_producers;
    old_producers.reserve(new_producers.size());
    
    auto prod_itr = producers_table.begin();
    while (prod_itr != producers_table.end()) {
        if (!active_only || prod_itr->is_active()) {
            old_producers.emplace_back(prod_itr->account);
        }
        ++prod_itr;
    }
    
    change_of_participants ret;
   
    std::set_difference(new_producers.begin(), new_producers.end(), old_producers.begin(), old_producers.end(), 
        std::inserter(ret.hired, ret.hired.begin()));
    
    std::set_difference(old_producers.begin(), old_producers.end(), new_producers.begin(), new_producers.end(),
        std::inserter(ret.fired, ret.fired.begin()));
    
    return ret;
}

void govern::setactprods(std::vector<name> pending_active_producers) {

    require_auth(_self);
    std::sort(pending_active_producers.begin(), pending_active_producers.end());
    auto state = state_singleton(_self, _self.value);
    auto s = state.get();
    
    s.pending_active_producers = pending_active_producers;
    state.set(s, _self);
    
}

bool govern::maybe_promote_active_producers(const structures::state_info& s) {
    if (!s.pending_active_producers.size()) {
        return false;
    }
    
    eosio_assert(s.block_num, "SYSTEM: incorrect block_num val");
    
    producers producers_table(_self, _self.value);
    auto change_of_producers = get_change_of_producers(producers_table, s.pending_active_producers, true);
    
    for (const auto& prod : change_of_producers.hired) {
        auto prod_itr = producers_table.find(prod.value);
        if (prod_itr != producers_table.end()) {
            producers_table.modify(prod_itr, name(), [&](auto& a) { a.commencement_block = s.block_num; });
        }
        else {
            producers_table.emplace(_self, [&]( auto &a ) { a = structures::producer {
                .account = prod,
                .votes = -1,
                .last_block_produced = s.block_num - 1,
                .commencement_block = s.block_num
            };});
        }
    }
    for (const auto& prod : change_of_producers.fired) {
        auto prod_itr = producers_table.find(prod.value);
        if (prod_itr != producers_table.end()) {
            producers_table.modify(prod_itr, name(), [&](auto& a) { a.commencement_block = 0; });
        }
    }
    
    return true;
}

void govern::shrink_to_active_producers(producers& producers_table, std::vector<std::pair<name, public_key> >& arg) {
    if (arg.size() > config::active_producers_num) {
        std::partial_sort(
            arg.begin() + config::active_top_producers_num, 
            arg.begin() + config::active_producers_num,
            arg.end(),
            [&producers_table](const std::pair<name, public_key>& lhs, const std::pair<name, public_key>& rhs) {
                auto lhs_prod_itr = producers_table.find(lhs.first.value);
                auto rhs_prod_itr = producers_table.find(rhs.first.value);

                auto lhs_last_block_produced = lhs_prod_itr != producers_table.end() ?
                    lhs_prod_itr->last_block_produced : std::numeric_limits<uint32_t>::max();
                auto rhs_last_block_produced = rhs_prod_itr != producers_table.end() ?
                    rhs_prod_itr->last_block_produced : std::numeric_limits<uint32_t>::max();
                return std::tie(lhs_last_block_produced, lhs.first) < std::tie(rhs_last_block_produced, rhs.first);
            });
        arg.resize(config::active_producers_num);
    }
}

void govern::reward_workers(structures::state_info& s) {
    if (s.funds) {
        INLINE_ACTION_SENDER(eosio::token, issue)(config::token_name, {config::issuer_name, config::active_name}, 
            {config::worker_name, asset(s.funds, system_token), ""});
        s.funds = 0;
    }
}

void govern::update_and_reward_producers(producers& producers_table, structures::state_info& s) {
    auto new_elected_producers = stake::get_top(config::elected_producers_num, system_token.code());
    eosio_assert(new_elected_producers.size() <= std::numeric_limits<uint16_t>::max(), "SYSTEM: incorrect producers num");
    if (new_elected_producers.size() < s.last_producers_num)
        return;
        
    std::vector<name> new_elected_producer_names;
    std::vector<std::pair<name, public_key> > new_producers_with_keys;
    new_elected_producer_names.reserve(new_elected_producers.size());
    new_producers_with_keys.reserve(new_elected_producers.size());
    std::map<name, int64_t> votes_map;
    int temp_i = 0;
    for (const auto& prod : new_elected_producers) {
        new_elected_producer_names.emplace_back(prod.account);
        new_producers_with_keys.emplace_back(std::make_pair(prod.account, prod.signing_key));
        votes_map[prod.account] = prod.votes;
    }
    
    shrink_to_active_producers(producers_table, new_producers_with_keys);
    
    auto packed_schedule = pack(new_producers_with_keys);
    if (set_proposed_producers(packed_schedule.data(),  packed_schedule.size()) >= 0) {
        s.last_producers_num = static_cast<uint16_t>(new_producers_with_keys.size());
    }
    
    auto change_of_producers = get_change_of_producers(producers_table, new_elected_producer_names, false);
    eosio_assert(s.block_num || change_of_producers.hired.empty(), "SYSTEM: incorrect block_num val");
    for (const auto& prod : change_of_producers.hired) {
        producers_table.emplace(_self, [&]( auto &a ) { a = structures::producer {
            .account = prod,
            .votes = 0, // see below
            .last_block_produced = s.block_num - 1
        };});
    }

    for (const auto& prod : change_of_producers.fired) {
        auto prod_itr = producers_table.find(prod.value);
        producers_table.modify(prod_itr, name(), [&](auto& a) { a.votes = -1; });
    }
    
    uint16_t actual_elected_num = 0;
    int64_t total_votes_for_elected = 0;
    for (auto prod_itr = producers_table.begin(); prod_itr != producers_table.end(); ++prod_itr) {
        if (prod_itr->is_elected()) {
            producers_table.modify(prod_itr, name(), [&](auto& a) { a.votes = votes_map[a.account]; });
            actual_elected_num++;
            total_votes_for_elected += prod_itr->votes;
        }
    }
    if (total_votes_for_elected) {
        auto reward_of_elected = safe_pct(s.funds, config::_100percent - config::workers_reward_pct);
        
        auto lucky_place = s.block_num % actual_elected_num;
        auto lucky_name = name();
        
        auto change = reward_of_elected;
        auto i = 0;
        for (auto prod_itr = producers_table.begin(); prod_itr != producers_table.end(); ++prod_itr) {
            if (prod_itr->is_elected()) {
                auto cur_reward = safe_prop(reward_of_elected, prod_itr->votes, total_votes_for_elected);
                producers_table.modify(prod_itr, name(), [&](auto& a) { 
                    a.pending_balance += cur_reward;
                });
                change -= cur_reward;
                if (i == lucky_place) {
                    lucky_name = prod_itr->account;
                }
                i++;
            }
        }
        eosio_assert(static_cast<bool>(lucky_name), "SYSTEM: !lucky_name");
        producers_table.modify(producers_table.find(lucky_name.value), name(), [&](auto& a) {
            a.pending_balance += change;
        });
        
        s.funds -= reward_of_elected;
    }
}

void govern::sum_up(producers& producers_table) {
    for (auto prod_itr = producers_table.begin(); prod_itr != producers_table.end();) {
        
        if (prod_itr->confirmed_balance > 0) {
            INLINE_ACTION_SENDER(cyber::stake, reward)(config::stake_name, {config::issuer_name, config::active_name},
                {prod_itr->account, asset(prod_itr->confirmed_balance, system_token)});
        }
        
        if (prod_itr->is_elected() || prod_itr->is_active()) {
            producers_table.modify(prod_itr, name(), [&](auto& a) { a.confirmed_balance = 0; });
            ++prod_itr;
        }
        else {
            prod_itr = producers_table.erase(prod_itr);
        }
    }
}

int64_t govern::get_target_emission_per_block(int64_t supply) const {
    auto votes_sum = cyber::stake::get_votes_sum(system_token.code());
    eosio_assert(votes_sum <= supply, "SYSTEM: incorrect votes_sum val");
    auto not_involved_pct = static_cast<decltype(config::_100percent)>(safe_prop(config::_100percent, supply - votes_sum, supply));
    auto arg = std::min(std::max(not_involved_pct, config::emission_min_arg), config::emission_max_arg);
    arg -= config::emission_min_arg;
    arg = safe_prop(config::_100percent, arg, config::emission_max_arg - config::emission_min_arg);
    auto emission_per_year_pct = (((arg * config::emission_factor) / config::_100percent) + config::emission_addition);  
    int64_t emission_per_year = safe_pct(emission_per_year_pct, supply);
    return emission_per_year / config::blocks_per_year;
}

}

EOSIO_DISPATCH( cyber::govern, (onblock)(setactprods)(setignored))
