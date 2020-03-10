#include <cyber.stake/cyber.stake.hpp>
#include <cyber.token/cyber.token.hpp>
#include <cyber.govern/cyber.govern.hpp>
#include <eosio/privileged.hpp>
#include <common/util.hpp>
#include <eosio/event.hpp>
#include <boost/container/flat_set.hpp>

using namespace cyber::config;

namespace cyber {

void govern::onblock(name producer, eosio::binary_extension<uint32_t> schedule_version) {
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
    producers producers_table(_self, _self.value);

    if (producer != config::internal_name && s.block_num != 1) {
        int64_t just_confirmed_balance = 0;
        auto utr = producers_table.find(producer.value);
        if (utr != producers_table.end() && !utr->is_empty()) {
            just_confirmed_balance = utr->unconfirmed_amount;
            producers_table.modify(utr, eosio::same_payer, [&](auto& u) {
                u.unconfirmed_amount = 0;
                u.omission_resets = 0;
                u.omission_count = 0;
                u.is_oblidged = false;
            });
        }
        auto b = balances_table.find(producer.value);
        if (b != balances_table.end()) {
            balances_table.modify(b, name(), [&](auto& b) { b.amount += block_reward + just_confirmed_balance; } );
        }
        else {
            balances_table.emplace(_self, [&](auto& b) { b = structures::balance_struct {
                .account = producer,
                .amount = block_reward + just_confirmed_balance
            };});
        }
    }
    
    if (s.block_num % config::reward_interval == 0) {
        reward_producers(producers_table, balances_table, s);
        reward_workers(s);
    }
    
    if ((s.block_num >= s.last_producers_num * schedule_period_factor + s.last_propose_block_num) || !s.last_propose_block_num) {
        propose_producers(s);
    }

    // the schedule version temporarily has the binary extension type only for the upgrade phase
    if (schedule_version.has_value()) {
        if (s.schedule_version.has_value() && s.schedule_version.value() != schedule_version.value()) {
            promote_producers(producers_table);
        }
        s.schedule_version.emplace(schedule_version.value());
    }

    state.set(s, _self);
}

void govern::reward_workers(structures::state_info& s) {
    if (s.funds) {
        INLINE_ACTION_SENDER(eosio::token, issue)(config::token_name, {config::issuer_name, config::active_name}, 
            {config::worker_name, asset(s.funds, system_token), ""});
        s.funds = 0;
    }
}

void govern::reward_producers(producers& producers_table, balances& balances_table, structures::state_info& s) {
    std::vector<std::pair<name, int64_t>> rewards;
    rewards.reserve(config::max_producers_num + 16);
    for (auto i = balances_table.begin(); i != balances_table.end();) {
        if (i->amount) {
            rewards.emplace_back(i->account, i->amount);
        }
        i = balances_table.erase(i);
    }
    
    if (rewards.size()) {
        INLINE_ACTION_SENDER(cyber::stake, reward)(config::stake_name, {config::issuer_name, config::active_name},
            {rewards, system_token});
    }
    
    auto top = stake::get_top(system_token.code(), s.required_producers_num + rewarded_for_votes_limit_displ, 0);
    auto actual_elected_num = top.size();
    int64_t votes_sum = 0;
    for (const auto& t : top) {
        votes_sum += t.votes;
    }
    
    auto reward_of_elected = safe_pct(s.funds, config::_100percent - config::workers_reward_pct);
    
    if (!votes_sum || !reward_of_elected) {
        return;
    }
    
    std::vector<std::pair<name, int64_t>> unconfirmed_rewards;
    unconfirmed_rewards.reserve(actual_elected_num + 16);
    auto change = reward_of_elected;
    for (const auto& t : top) {
        auto cur_reward = safe_prop(reward_of_elected, t.votes, votes_sum);
        if (cur_reward) {
            unconfirmed_rewards.emplace_back(t.account, cur_reward);
            change -= cur_reward;
        }
    }
    if (change) {
        auto idx = s.block_num % actual_elected_num;
        if (idx >= unconfirmed_rewards.size()) {
            unconfirmed_rewards.emplace_back(top[idx].account, change);
        } else {
            unconfirmed_rewards[idx].second += change;
        }
    }
    s.funds -= reward_of_elected;

    for (auto& r : unconfirmed_rewards) {
        auto b = producers_table.find(r.first.value);
        if (b != producers_table.end()) {
            producers_table.modify(b, name(), [&](auto& b) {
                b.unconfirmed_amount += r.second;
            });
        }
        else {
            producers_table.emplace(_self, [&](auto& b) {
                b.is_oblidged = false;
                b.account = r.first;
                b.unconfirmed_amount = r.second;
            });
        }
    }
}

void govern::propose_producers(structures::state_info& s) {
    s.last_propose_block_num = s.block_num;

    if (!s.last_resize_step.has_value()) {
        s.last_resize_step.emplace(eosio::current_time_point());
    }
    if (!s.resize_shift.has_value()) {
        s.resize_shift.emplace(1);
    }

    if ((eosio::current_time_point() - s.last_resize_step.value()).to_seconds() >= schedule_resize_min_delay) {
        s.required_producers_num += s.resize_shift.value();
        s.required_producers_num = std::min(std::max(s.required_producers_num, min_producers_num), max_producers_num);
        
        s.last_resize_step.emplace(eosio::current_time_point());
    }

    auto new_producers = stake::get_top(system_token.code(), s.required_producers_num - active_reserve_producers_num, active_reserve_producers_num);
    auto new_producers_num = new_producers.size();
    
    auto min_new_producers_num = s.last_producers_num;
    if (s.resize_shift.value() < 0) {
        min_new_producers_num -= std::min<decltype(min_new_producers_num)>(min_new_producers_num, std::abs(s.resize_shift.value()));
    }
    if (new_producers_num < min_new_producers_num) {
        return;
    }
    
    std::vector<eosio::producer_key> schedule;
    schedule.reserve(new_producers_num + 16);
    for (const auto& t : new_producers) {
        schedule.emplace_back(eosio::producer_key{t.account, t.signing_key});
    }
    if (!eosio::set_proposed_producers(schedule)) {
        return;
    }
    s.last_producers_num = new_producers_num;
    std::vector<name> accounts;
    accounts.reserve(new_producers_num + 16);
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

void govern::setshift(int8_t shift) {
    eosio::check(schedule_size_shift_min <= shift && shift <= schedule_size_shift_max, "incorrect shift");
    require_auth(producers_name);
    auto state = state_singleton(_self, _self.value);
    auto s = state.get(); // no default values, because it was created on the first block, errors can happens only in tests
    eosio::check(shift != s.resize_shift.value(), "the shift has not changed");
    s.resize_shift.emplace(shift);
    state.set(s, _self);
}

void govern::promote_producers(producers& producers_table) {
    static constexpr auto token_code = system_token.code();
    auto obliged_idx = producers_table.get_index<"byoblidged"_n>();
    boost::container::flat_set<eosio::name> active_producers;

    active_producers.reserve(config::max_producers_num + 16);
    for (const auto& acc: eosio::get_active_producers()) {
        active_producers.insert(acc);
    }
    for (auto itr = obliged_idx.begin(); itr != obliged_idx.end() && itr->is_oblidged; ) {
        bool should_erase = false;
        if (!cyber::stake::candidate_exists(itr->account, token_code)) {
            should_erase = true;
        } else if (itr->omission_resets >= config::resets_limit) {
            INLINE_ACTION_SENDER(cyber::stake, setproxylvl)(config::stake_name, {config::issuer_name, config::active_name},
                {itr->account, token_code, stake::get_max_level(token_code)}); // agent cannot disappear
            should_erase = true;
        }

        auto atr = active_producers.find(itr->account);
        if (should_erase) {
            if (active_producers.end() != atr) {
                active_producers.erase(atr);
            }
            burn_reward(itr->account, itr->amount + itr->unconfirmed_amount);
            itr = obliged_idx.erase(itr);
            continue;
        }

        burn_reward(itr->account, itr->unconfirmed_amount);

        obliged_idx.modify(itr, eosio::same_payer, [&](auto& o){
            if (active_producers.end() != atr) {
                active_producers.erase(atr);
            } else {
                o.is_oblidged = false;
            }

            o.unconfirmed_amount = 0;
            o.omission_count += 1;

            if (o.omission_count >= config::omission_limit) {
                INLINE_ACTION_SENDER(cyber::stake, setkey)(config::stake_name, {config::issuer_name, config::active_name},
                    {o.account, token_code, public_key{}});

                o.omission_count = 0;
                o.omission_resets += 1;
            }
        });
        ++itr;
    }

    for (const auto& acc : active_producers) {
        auto itr = producers_table.find(acc.value);
        if (producers_table.end() == itr) {
            producers_table.emplace(_self, [&](auto& p) {
                p.account = acc;
                p.is_oblidged = true;
            });
        } else if (!itr->is_oblidged) {
            producers_table.modify(itr, eosio::same_payer, [&](auto& p) {
                p.is_oblidged = true;
            });
        }
    }
}

}
