#include <cyber.stake/cyber.stake.hpp>
#include <cyber.token/cyber.token.hpp>
#include <cyber.govern/cyber.govern.hpp>
#include <eosio/privileged.hpp>
#include <common/util.hpp>
#include <eosio/event.hpp>

using namespace cyber::config;

namespace cyber {

void govern::onblock(name producer) {
    require_auth(_self);
    
    auto state = state_singleton(_self, _self.value);
    auto s = state.get_or_default(structures::state_info { .last_schedule_increase = eosio::current_time_point() });
    
    s.block_num++;
    
    int64_t block_reward = 0;
    if (producer != config::internal_name) {
        auto supply     = eosio::token::get_supply    (config::token_name, system_token.code()).amount;
        auto max_supply = eosio::token::get_max_supply(config::token_name, system_token.code()).amount;
        eosio::check(max_supply >= supply, "SYSTEM: incorrect supply");
        
        if ((s.block_num % config::update_emission_per_block_interval == 0) || !s.target_emission_per_block) {
            s.target_emission_per_block = get_target_emission_per_block(supply);
        }
        auto cur_block_emission = std::min(max_supply - supply, s.target_emission_per_block);
        block_reward = safe_pct(cur_block_emission, config::block_reward_pct);
        s.funds += cur_block_emission - block_reward;
    }
        
    balances balances_table(_self, _self.value);
    
    obliged_producers obliged_prods_table(_self, _self.value);
    omissions omissions_table(_self, _self.value);
    unconfirmed_balances unconfirmed_balances_table(_self, _self.value);
    
    if (producer != config::internal_name && s.block_num != 1) {
        int64_t just_confirmed_balance = 0;
        auto u = unconfirmed_balances_table.find(producer.value);
        if (u != unconfirmed_balances_table.end()) {
            just_confirmed_balance = u->amount;
            unconfirmed_balances_table.erase(u);
        }
        auto p = obliged_prods_table.find(producer.value);
        if (p != obliged_prods_table.end()) {
            obliged_prods_table.erase(p);
        }
        auto o = omissions_table.find(producer.value);
        if (o != omissions_table.end()) {
            omissions_table.erase(o);
        }
        auto b = balances_table.find(producer.value);
        if (b != balances_table.end()) {
            balances_table.modify(b, name(), [&](auto& b) { b.amount += block_reward + just_confirmed_balance; } );
        }
        else {
            balances_table.emplace(_self, [&](auto& b) { b = structures::balance {
                .account = producer,
                .amount = block_reward + just_confirmed_balance
            };});
        }
    }
    
    if (s.block_num % config::reward_interval == 0) {
        reward_producers(balances_table, s);
        reward_workers(s);
    }
    
    if ((s.block_num >= s.last_producers_num * schedule_period_factor + s.last_propose_block_num) || !s.last_propose_block_num) {
        propose_producers(s);
    }
    maybe_promote_producers();

    state.set(s, _self);
}

void govern::reward_workers(structures::state_info& s) {
    if (s.funds) {
        INLINE_ACTION_SENDER(eosio::token, issue)(config::token_name, {config::issuer_name, config::active_name}, 
            {config::worker_name, asset(s.funds, system_token), ""});
        s.funds = 0;
    }
}

void govern::reward_producers(balances& balances_table, structures::state_info& s) {
    std::map<name, int64_t> rewards;
    for (auto i = balances_table.begin(); i != balances_table.end();) {
        if (i->amount) {
            rewards[i->account] += i->amount;
        }
        i = balances_table.erase(i);
    }
    
    if (rewards.size()) {
        INLINE_ACTION_SENDER(cyber::stake, reward)(config::stake_name, {config::issuer_name, config::active_name},
            {std::vector<std::pair<name, int64_t> >(rewards.begin(), rewards.end()), system_token});
    }
    
    auto top = stake::get_top(system_token.code(), s.required_producers_num + rewarded_for_votes_limit_displ, 0, false);
    
    auto actual_elected_num = top.size();
    int64_t votes_sum = 0;
    for (const auto& t : top) {
        votes_sum += t.votes;
    }

    auto reward_of_elected = safe_pct(s.funds, config::_100percent - config::workers_reward_pct);
    
    if (!votes_sum || !reward_of_elected) {
        return;
    }
    
    std::map<name, int64_t> unconfirmed_rewards;
    auto change = reward_of_elected;
    for (size_t i = 0; i < actual_elected_num; i++) {
        auto cur_reward = safe_prop(reward_of_elected, top[i].votes, votes_sum);
        if (cur_reward) {
            unconfirmed_rewards[top[i].account] += cur_reward;
            change -= cur_reward;
        }
    }
    if (change) {
        unconfirmed_rewards[top[s.block_num % actual_elected_num].account] += change;
    }
    s.funds -= reward_of_elected;
    
    unconfirmed_balances unconfirmed_balances_table(_self, _self.value);
    for (auto& r : unconfirmed_rewards) {
        auto b = unconfirmed_balances_table.find(r.first.value);
        if (b != unconfirmed_balances_table.end()) {
            unconfirmed_balances_table.modify(b, name(), [&](auto& b) { b.amount += r.second; } );
        }
        else {
            unconfirmed_balances_table.emplace(_self, [&](auto& b) { b = structures::balance {
                .account = r.first,
                .amount = r.second
            };});
        }
    }
}

void govern::propose_producers(structures::state_info& s) {
    s.last_propose_block_num = s.block_num;
    
    auto sched_state = schedule_resize_singleton(_self, _self.value);
    auto sched = sched_state.get_or_default(structures::schedule_resize_info { .last_step = eosio::current_time_point() });
    
    if ((eosio::current_time_point() - sched.last_step).to_seconds() >= schedule_resize_min_delay) {
        s.required_producers_num += sched.shift;
        s.required_producers_num = std::min(std::max(s.required_producers_num, min_producers_num), max_producers_num);
        
        sched.last_step = eosio::current_time_point();
    }
    sched_state.set(sched, _self);
    
    auto new_producers = stake::get_top(system_token.code(), s.required_producers_num - active_reserve_producers_num, active_reserve_producers_num);
    auto new_producers_num = new_producers.size();
    
    auto min_new_producers_num = s.last_producers_num;
    if (sched.shift < 0) {
        min_new_producers_num -= std::min<decltype(min_new_producers_num)>(min_new_producers_num, std::abs(sched.shift));
    }
    if (new_producers_num < min_new_producers_num) {
        return;
    }
    
    std::vector<eosio::producer_key> schedule;
    schedule.reserve(new_producers_num);
    for (const auto& t : new_producers) {
        schedule.emplace_back(eosio::producer_key{t.account, t.signing_key});
    }
    if (!eosio::set_proposed_producers(schedule)) {
        return;
    }
    s.last_producers_num = new_producers_num;
    std::vector<name> accounts;
    accounts.reserve(new_producers_num);
    for (const auto& t : new_producers) {
        accounts.emplace_back(t.account);
    }
    
    INLINE_ACTION_SENDER(cyber::stake, pick)(config::stake_name, {config::issuer_name, config::active_name},
        {system_token.code(), accounts});
}

int64_t govern::get_target_emission_per_block(int64_t supply) const {
    auto votes_sum = cyber::stake::get_votes_sum(system_token.code());
    eosio::check(votes_sum <= supply, "SYSTEM: incorrect votes_sum val");
    auto not_involved_pct = static_cast<decltype(config::_100percent)>(safe_prop(config::_100percent, supply - votes_sum, supply));
    auto arg = std::min(std::max(not_involved_pct, config::emission_min_arg), config::emission_max_arg);
    arg -= config::emission_min_arg;
    arg = safe_prop(config::_100percent, arg, config::emission_max_arg - config::emission_min_arg);
    auto emission_per_year_pct = (((arg * config::emission_factor) / config::_100percent) + config::emission_addition);  
    int64_t emission_per_year = safe_pct(emission_per_year_pct, supply);
    return emission_per_year / config::blocks_per_year;
}

void govern::setactprods(std::vector<name> pending_active_producers) {
    require_auth(_self);
    pending_producers pending_prods_table(_self, _self.value);
    auto prods = pending_prods_table.get_or_default(structures::pending_producers_state{});
    if (!prods.accounts.empty()) {
        eosio::print("WARNING! govern::setactprods, pending_prods_table was not empty\n");
    }
    prods.accounts = pending_active_producers;
    pending_prods_table.set(prods, _self);
}

void govern::setshift(int8_t shift) {
    eosio::check(schedule_size_shift_min <= shift && shift <= schedule_size_shift_max, "incorrect shift");
    require_auth(producers_name);
    auto sched_state = schedule_resize_singleton(_self, _self.value);
    auto sched = sched_state.get_or_default(structures::schedule_resize_info { .last_step = eosio::current_time_point() });
    eosio::check(shift != sched.shift, "the shift has not changed");
    sched.shift = shift;
    sched_state.set(sched, _self);
}

void govern::maybe_promote_producers() {
    pending_producers pending_prods_table(_self, _self.value);
    auto prods = pending_prods_table.get_or_default(structures::pending_producers_state{});
    if (prods.accounts.empty()) {
        return;
    }
    obliged_producers obliged_prods_table(_self, _self.value);
    omissions omissions_table(_self, _self.value);
    unconfirmed_balances unconfirmed_balances_table(_self, _self.value);
    
    for (auto i = obliged_prods_table.begin(); i != obliged_prods_table.end();) {
        auto o = omissions_table.find(i->account.value);
        if (o != omissions_table.end()) {
            omissions_table.modify(o, name(), [&](auto& o) { o.count += 1; } );
        }
        else {
            omissions_table.emplace(_self, [&](auto& o) { o = structures::omission {
                .account = i->account,
                .count = 1
            };});
        }
        
        auto b = unconfirmed_balances_table.find(i->account.value);
        if (b != unconfirmed_balances_table.end()) {
            eosio::event(_self, "burnreward"_n, *b).send();
            unconfirmed_balances_table.erase(b);
        }
        i = obliged_prods_table.erase(i);
    }
    symbol_code token_code = system_token.code();
    
    auto omissions_idx = omissions_table.get_index<"bycount"_n>();
    auto omission_itr = omissions_idx.lower_bound(std::numeric_limits<decltype(structures::omission::count)>::max());
    if (omission_itr != omissions_idx.end() && omission_itr->count >= config::omission_limit) {
        if (cyber::stake::candidate_exists(omission_itr->account, token_code)) {
            INLINE_ACTION_SENDER(cyber::stake, setkey)(config::stake_name, {config::stake_name, config::active_name},
                {omission_itr->account, token_code, public_key{}});
        }
        omissions_idx.erase(omission_itr);
    }
    
    for (const auto& acc : prods.accounts) {
        obliged_prods_table.emplace(_self, [&](auto& p) { p = structures::producer { .account = acc }; });
    }
    prods.accounts.clear();
    pending_prods_table.set(prods, _self);
}

}

EOSIO_DISPATCH( cyber::govern, (onblock)(setactprods)(setshift))
