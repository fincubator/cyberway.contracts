/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#undef CHAINDB_ANOTHER_CONTRACT_PROTECT
#define CHAINDB_ANOTHER_CONTRACT_PROTECT(_CHECK, _MSG)
   
#include <algorithm>
#include <cyber.stake/cyber.stake.hpp>
#include <cyber.token/cyber.token.hpp>
#include <common/dispatchers.hpp>
#include <common/parameter_ops.hpp>
#include <common/util.hpp>

namespace cyber {
    
const stake::structures::param::purpose& stake::structures::param::get_purpose(symbol_code purpose_code) const {
    auto ret_itr = std::find_if(purposes.begin(), purposes.end(),
                [purpose_code](const structures::param::purpose &p) { return p.code == purpose_code; });
    eosio_assert(ret_itr != purposes.end(), ("unknown purpose: " + purpose_code.to_string()).c_str());
    return *ret_itr;
}

size_t stake::structures::param::get_purpose_index(symbol_code purpose_code) const {
    auto ret_itr = std::find_if(purposes.begin(), purposes.end(),
                [purpose_code](const structures::param::purpose &p) { return p.code == purpose_code; });
    eosio_assert(ret_itr != purposes.end(), ("unknown purpose: " + purpose_code.to_string()).c_str());
    return std::distance(purposes.begin(), ret_itr);
}
    
const auto& stake::get_param(const stake::params& params_table, symbol_code purpose_code, symbol_code token_code, bool strict) const {
    const auto& ret = params_table.get(token_code.raw(), "no staking for token");
    if(strict) {
        ret.check_purpose(purpose_code);
    }
    return ret;
}
    
void stake::delegate_traversal(symbol_code token_code, agents_idx_t& agents_idx, grants_idx_t& grants_idx, 
        name agent_name, const std::vector<int64_t>& amount, std::vector<int64_t>& ret, bool refill) {
    auto purposes_num = amount.size();
    eosio_assert(purposes_num == ret.size(), "SYSTEM: incorrect amount.size or ret.size");
    auto agent = get_agent_itr(token_code, agents_idx, agent_name);
    int64_t own_funds = 0;
    for (const auto& p : agent->purpose_funds) {
        auto total_funds = p.get_total_funds();
        eosio_assert(p.shares_sum || (total_funds == 0), "SYSTEM: incorrect total_funds or shares_sum");
        own_funds += safe_prop(total_funds, p.own_share, p.shares_sum);
    }
    
    eosio_assert((own_funds >= agent->min_own_staked) || refill, "insufficient agent funds");
    
    auto remaining_amount = amount;
    std::vector<int64_t> to_delegate(purposes_num);
    std::vector<int64_t> delegated(purposes_num);
    auto grant_itr = grants_idx.lower_bound(std::make_tuple(token_code, agent_name, name()));
    while ((grant_itr != grants_idx.end()) && (grant_itr->token_code == token_code) && (grant_itr->grantor_name == agent_name)) {
        
        for (size_t i = 0; i < purposes_num; i++) {
            to_delegate[i] = safe_pct(amount[i], grant_itr->pct);
            remaining_amount[i] -= to_delegate[i];
            eosio_assert(remaining_amount[i] >= 0, "SYSTEM: incorrect remaining_amount");
        }
        
        delegate_traversal(token_code, agents_idx, grants_idx, grant_itr->agent_name, to_delegate, delegated, true);
        grants_idx.modify(grant_itr, name(), [&](auto& g) {
            for (size_t i = 0; i < purposes_num; i++) {
                g.purpose_grants[i].share += delegated[i];
                g.purpose_grants[i].granted += to_delegate[i];
            }
        });
        ++grant_itr;
    }
    for (size_t i = 0; i < purposes_num; i++) {
        auto total_funds = agent->purpose_funds[i].get_total_funds();
        ret[i] = total_funds ? safe_prop(agent->purpose_funds[i].shares_sum, amount[i], total_funds) : amount[i];
    }
    
    agents_idx.modify(agent, name(), [&](auto& a) {
        for (size_t i = 0; i < purposes_num; i++) {
            a.purpose_funds[i].balance += remaining_amount[i];
            a.purpose_funds[i].proxied += amount[i] - remaining_amount[i];
            a.purpose_funds[i].shares_sum += ret[i];
        }
    });
}

void stake::delegate(name grantor_name, name agent_name, symbol_code token_code, int16_t pct) {
    require_auth(grantor_name);
    eosio_assert(pct > 0, "pct must be positive");
    params params_table(table_owner, table_owner.value);
    const auto& param = get_param(params_table, symbol_code(), token_code, false);
    
    update_stake_proxied(token_code, agent_name, param.frame_length, true);
    
    agents agents_table(table_owner, table_owner.value);
    auto agents_idx = agents_table.get_index<"bykey"_n>();
    
    auto grantor_as_agent = get_agent_itr(token_code, agents_idx, grantor_name);
    auto purposes_num = param.purposes.size();
    std::vector<int64_t> amount(purposes_num);
    for (size_t i = 0; i < purposes_num; i++) {
        amount[i] = safe_pct(grantor_as_agent->purpose_funds[i].balance, pct);
    }
    
    auto agent = get_agent_itr(token_code, agents_idx, agent_name);
    grants grants_table(table_owner, table_owner.value);
    auto grants_idx = grants_table.get_index<"bykey"_n>();
    
    std::vector<int64_t> delegated(purposes_num);
    delegate_traversal(token_code, agents_idx, grants_idx, agent_name, amount, delegated);
    uint8_t proxies_num = 0;
    auto grant_itr = grants_idx.lower_bound(std::make_tuple(token_code, grantor_name, name()));
    bool agent_found = false;
    while ((grant_itr != grants_idx.end()) && (grant_itr->token_code == token_code) && (grant_itr->grantor_name == grantor_name)) {
        ++proxies_num;
        if (grant_itr->agent_name == agent_name) {
            grants_idx.modify(grant_itr, name(), [&](auto& g) {
                for (size_t i = 0; i < purposes_num; i++) {
                    g.purpose_grants[i].share += delegated[i];
                    g.purpose_grants[i].granted += amount[i];
                }
            });
            agent_found = true;
        }
        ++grant_itr;
    }
    
    if (!agent_found) {
        eosio_assert(proxies_num < param.max_proxies[grantor_as_agent->proxy_level - 1], "proxy cannot be added");
        eosio_assert(grantor_as_agent->proxy_level > agent->proxy_level, 
            ("incorrect proxy levels: grantor " + std::to_string(grantor_as_agent->proxy_level) + 
            ", agent " + std::to_string(agent->proxy_level)).c_str());
        auto purposes_num = param.purposes.size();
        grants_table.emplace(grantor_as_agent->account, [&](auto& g) {
            g = structures::grant {
                .id = grants_table.available_primary_key(),
                .token_code = token_code,
                .grantor_name = grantor_as_agent->account,
                .agent_name = agent->account,
                .purpose_grants = std::vector<structures::grant::purpose_grant_t>(purposes_num),
                .pct = 0,
                .break_fee = agent->fee,
                .break_min_own_staked = agent->min_own_staked
            };
            for (size_t i = 0; i < purposes_num; i++) {
                g.purpose_grants[i].share += delegated[i];
                g.purpose_grants[i].granted += amount[i];
            }
        });   
    }
    
    agents_idx.modify(grantor_as_agent, name(), [&](auto& a) {
        for (size_t i = 0; i < purposes_num; i++) {
            a.purpose_funds[i].balance -= amount[i];
            a.purpose_funds[i].proxied += amount[i];
        }
    });
}

void stake::recall(name grantor_name, name agent_name, symbol_code token_code, int16_t pct) {
    ::recall_stake_proxied(token_code.raw(), grantor_name.value, agent_name.value, pct);
}

void stake::check_grant_terms(const structures::agent& agent, int16_t break_fee, int64_t break_min_own_staked) {
    eosio_assert(break_fee < 0 || agent.fee <= break_fee, "break_fee can't be less than current agent fee");
    eosio_assert(break_min_own_staked <= agent.min_own_staked, "break_min_own_staked can't be greater than current min_own_staked value");
}

void stake::setgrntterms(name grantor_name, name agent_name, symbol_code token_code, int16_t pct, int16_t break_fee, int64_t break_min_own_staked) {
    eosio_assert(0 <= pct && pct <= config::_100percent, "pct must be between 0% and 100% (0-10000)");
    eosio_assert(0 <= break_fee && break_fee <= config::_100percent, "break_fee must be between 0% and 100% (0-10000)");
    eosio_assert(0 <= break_min_own_staked, "break_min_own_staked can't be negative");
    
    require_auth(grantor_name);
    params params_table(table_owner, table_owner.value);
    const auto& param = get_param(params_table, symbol_code(), token_code, false);
    
    agents agents_table(table_owner, table_owner.value);
    auto agents_idx = agents_table.get_index<"bykey"_n>();
    grants grants_table(table_owner, table_owner.value);
    auto grants_idx = grants_table.get_index<"bykey"_n>();

    int16_t pct_sum = 0;
    bool changed = false;
    bool agent_found = false;
    uint8_t proxies_num = 0;
    auto grant_itr = grants_idx.lower_bound(std::make_tuple(token_code, grantor_name, name()));
    while ((grant_itr != grants_idx.end()) && (grant_itr->token_code == token_code) && (grant_itr->grantor_name == grantor_name)) {
         ++proxies_num;
        if (grant_itr->agent_name == agent_name) {
            check_grant_terms(*get_agent_itr(token_code, agents_idx, agent_name), break_fee, break_min_own_staked);
            changed = changed
                || grant_itr->pct != pct
                || grant_itr->break_fee != break_fee
                || grant_itr->break_min_own_staked != break_min_own_staked;
            agent_found = true;
            
            bool has_a_share = false;
            for (auto& p : grant_itr->purpose_grants) {
                if (p.share) {
                    has_a_share = true;
                    break;
                }
            }
            if(pct || has_a_share) {
                grants_idx.modify(grant_itr, name(), [&](auto& g) {
                    g.pct = pct;
                    g.break_fee = break_fee;
                    g.break_min_own_staked = break_min_own_staked;
                });
                pct_sum += pct;
                ++grant_itr;
            }
            else
                grant_itr = grants_idx.erase(grant_itr);
        }
        else {
            pct_sum += grant_itr->pct;
            ++grant_itr;
        }
        eosio_assert(pct_sum <= config::_100percent, "too high pct value\n");
    }
    if (!agent_found && pct) {
        pct_sum += pct;
        eosio_assert(pct_sum <= config::_100percent, "too high pct value\n");
        auto grantor_as_agent = get_agent_itr(token_code, agents_idx, grantor_name, &param, &agents_table);
        eosio_assert(proxies_num < param.max_proxies[grantor_as_agent->proxy_level - 1], "proxy cannot be added");
        auto agent = get_agent_itr(token_code, agents_idx, agent_name);
        check_grant_terms(*agent, break_fee, break_min_own_staked);
        update_stake_proxied(token_code, agent_name, param.frame_length, true);
        
        eosio_assert(grantor_as_agent->proxy_level > agent->proxy_level, 
            ("incorrect proxy levels: grantor " + std::to_string(grantor_as_agent->proxy_level) + 
            ", agent " + std::to_string(agent->proxy_level)).c_str());
        grants_table.emplace(grantor_as_agent->account, [&]( auto &item ) { item = structures::grant {
            .id = grants_table.available_primary_key(),
            .token_code = token_code,
            .grantor_name = grantor_as_agent->account,
            .agent_name = agent->account,
            .purpose_grants = std::vector<structures::grant::purpose_grant_t>(param.purposes.size()),
            .pct = pct,
            .break_fee = break_fee,
            .break_min_own_staked = break_min_own_staked
        };});    
            
        changed = true;
    }

    eosio_assert(changed, "agent pct has not been changed");
}

void stake::on_transfer(name from, name to, asset quantity, std::string memo) {
    if (_self != to || memo == config::reward_memo)
        return;
    
    auto token_code = quantity.symbol.code();
    auto purpose_code = symbol_code(memo.c_str());
    params params_table(table_owner, table_owner.value);
    
    const auto& param = get_param(params_table, purpose_code, token_code);
    
    agents agents_table(table_owner, table_owner.value);
    auto agents_idx = agents_table.get_index<"bykey"_n>();
    
    auto agent = get_agent_itr(token_code, agents_idx, from, &param, &agents_table);
    update_stake_proxied(token_code, from, param.frame_length, false);
    grants grants_table(table_owner, table_owner.value);
    auto grants_idx = grants_table.get_index<"bykey"_n>();
    auto purposes_num = param.purposes.size();
    auto purpose_index = param.get_purpose_index(purpose_code);
    
    std::vector<int64_t> amount(purposes_num, 0);
    amount[purpose_index] += quantity.amount;
    std::vector<int64_t> share(purposes_num);
    
    delegate_traversal(token_code, agents_idx, grants_idx, from, amount, share);
    agents_idx.modify(agent, name(), [&](auto& a) {
        a.purpose_funds[purpose_index].own_share += share[purpose_index]; 
    });
    update_stats(structures::stat {
        .id = 0,
        .token_code = token_code,
        .purpose_code = purpose_code,
        .total_staked = quantity.amount
    }, from);
}

void stake::claim(name account, symbol_code token_code, symbol_code purpose_code) {
    update_payout(account, asset(0, symbol(token_code, 0)), purpose_code, true);
}

void stake::withdraw(name account, asset quantity, symbol_code purpose_code) {
    eosio_assert(quantity.amount > 0, "must withdraw positive quantity");
    update_payout(account, quantity, purpose_code);
}

void stake::cancelwd(name account, asset quantity, symbol_code purpose_code) {
    eosio_assert(quantity.amount >= 0, "quantity can't be negative");
    quantity.amount = -quantity.amount;
    update_payout(account, quantity, purpose_code);
}

void stake::update_payout(name account, asset quantity, symbol_code purpose_code, bool claim_mode) {
    require_auth(account); 
    auto token_code = quantity.symbol.code();
    params params_table(table_owner, table_owner.value);
    const auto& param = get_param(params_table, purpose_code, token_code);
    payouts payouts_table(_self, purpose_code.raw());
    auto& purpose_param = param.get_purpose(purpose_code);
    send_scheduled_payout(payouts_table, account, purpose_param.payout_step_lenght, param.token_symbol);
    
    if(claim_mode)
        return;
    
    update_stake_proxied(token_code, account, param.frame_length, false);
     
    agents agents_table(table_owner, table_owner.value);
    auto agents_idx = agents_table.get_index<"bykey"_n>();
    auto agent = get_agent_itr(token_code, agents_idx, account);

    grants grants_table(table_owner, table_owner.value);
    auto grants_idx = grants_table.get_index<"bykey"_n>();
    auto purpose_index = param.get_purpose_index(purpose_code);
    
    auto total_funds = agent->purpose_funds[purpose_index].get_total_funds();
    auto shares_sum = agent->purpose_funds[purpose_index].shares_sum;
    eosio_assert(shares_sum || (total_funds == 0), "SYSTEM: incorrect total_funds or shares_sum");
    
    int64_t balance_diff = 0;
    int64_t shares_diff = 0;
    if (quantity.amount > 0) {
        eosio_assert(quantity.amount <= agent->purpose_funds[purpose_index].balance, "insufficient funds");

        eosio_assert(total_funds > 0, "no funds to withdrawal");
        auto own_funds = safe_prop(total_funds, agent->purpose_funds[purpose_index].own_share, shares_sum);
        eosio_assert(own_funds - quantity.amount >= agent->min_own_staked, "insufficient agent funds");

        balance_diff = -quantity.amount;
        shares_diff = -safe_prop(shares_sum, quantity.amount, total_funds);
        eosio_assert(-shares_diff <= agent->purpose_funds[purpose_index].own_share, "SYSTEM: incorrect shares_to_withdraw val");
        
        payouts_table.emplace(account, [&]( auto &item ) { item = structures::payout {
            .id = payouts_table.available_primary_key(),
            .token_code = token_code,
            .account = account,
            .balance = quantity.amount,
            .steps_left = purpose_param.payout_steps_num,
            .last_step = time_point_sec(::now())
        };});
    }
    else {
        quantity.amount = -quantity.amount;
        auto acc_index = payouts_table.get_index<"payoutacc"_n>();
        auto payout_lb_itr = acc_index.lower_bound(std::make_tuple(token_code, account));

        int64_t amount_sum = 0;
        auto payout_itr = payout_lb_itr;
        while ((payout_itr != acc_index.end()) && (payout_itr->token_code == token_code) && (payout_itr->account == account)) {
            amount_sum += payout_itr->balance;
        }
        eosio_assert(amount_sum >= quantity.amount, "insufficient funds");
        
        while ((payout_itr != acc_index.end()) && (payout_itr->token_code == token_code) && (payout_itr->account == account)) {
            auto cur_amount = quantity.amount ? safe_prop(payout_itr->balance, quantity.amount, amount_sum) : payout_itr->balance;
            balance_diff += cur_amount;
            if (cur_amount < payout_itr->balance) {
                acc_index.modify(payout_itr, name(), [&](auto& p) { p.balance -= cur_amount; });
                ++payout_itr;
            }
            else
                payout_itr = acc_index.erase(payout_itr);
        }
        //TODO:? due to rounding, balance_diff usually will be less than requested. should we do something about it?
        shares_diff = total_funds ? safe_prop(shares_sum, balance_diff, total_funds) : balance_diff;
    }
    
    agents_idx.modify(agent, name(), [&](auto& a) {
        a.purpose_funds[purpose_index].balance += balance_diff;
        a.purpose_funds[purpose_index].shares_sum += shares_diff;
        a.purpose_funds[purpose_index].own_share += shares_diff;
    });
    
    update_stats(structures::stat {
        .id = 0,
        .token_code = token_code,
        .purpose_code = purpose_code,
        .total_staked = balance_diff
    });
}
 
void stake::update_stats(const structures::stat& stat_arg, name payer) {
    stats stats_table(table_owner, table_owner.value);
    auto stat_index = stats_table.get_index<"bykey"_n>();
    auto stat = stat_index.find(std::make_tuple(stat_arg.purpose_code, stat_arg.token_code));

    if (stat == stat_index.end() && payer != name()) {
        eosio_assert(stat_arg.total_staked >= 0, "SYSTEM: incorrect total_staked");
        stats_table.emplace(payer, [&](auto& s) { s = stat_arg; s.id = stats_table.available_primary_key(); });
    }
    else if (stat != stat_index.end()) {
        stat_index.modify(stat, name(), [&](auto& s) { 
            s.total_staked += stat_arg.total_staked; 
            s.enabled = s.enabled || stat_arg.enabled;
        });
    }
    else {
        eosio_assert(false, "stats doesn't exist");
    }
}

void stake::send_scheduled_payout(payouts& payouts_table, name account, int64_t payout_step_lenght, symbol sym) {
    const int64_t now = ::now();
    eosio_assert(payout_step_lenght > 0, "SYSTEM: incorrect payout_step_lenght val");
    
    auto acc_index = payouts_table.get_index<"payoutacc"_n>();
    auto payout_itr = acc_index.lower_bound(std::make_tuple(sym.code(), account)); 
    int64_t amount = 0;
    while ((payout_itr != acc_index.end()) && (payout_itr->token_code == sym.code()) && (payout_itr->account == account)) {
        auto seconds_passed = now - payout_itr->last_step.utc_seconds;
        eosio_assert(seconds_passed >= 0, "SYSTEM: incorrect seconds_passed val");
        auto steps_passed = std::min(seconds_passed / payout_step_lenght, static_cast<int64_t>(payout_itr->steps_left));
        if (steps_passed) {
            if(steps_passed != payout_itr->steps_left) {
                auto cur_amount = safe_prop(payout_itr->balance, steps_passed, payout_itr->steps_left);
                acc_index.modify(payout_itr, name(), [&](auto& p) {
                    p.balance -= cur_amount;
                    p.steps_left -= steps_passed;
                    p.last_step = time_point_sec(p.last_step.utc_seconds + (steps_passed * payout_step_lenght));
                });
                ++payout_itr;
            }
            else {
                amount += payout_itr->balance;
                payout_itr = acc_index.erase(payout_itr);
            }
        }
        else
            ++payout_itr;
    }
    if(amount) {
        INLINE_ACTION_SENDER(eosio::token, transfer)(config::token_name, {_self, config::active_name},
            {_self, account, asset(amount, sym), "unstaked tokens"});
    }
}

void stake::setproxyfee(name account, symbol_code token_code, int16_t fee) {
    eosio_assert(0 <= fee && fee <= config::_100percent, "fee must be between 0% and 100% (0-10000)");
    modify_agent(account, token_code, [fee](auto& a) { a.fee = fee; } );
}

void stake::setminstaked(name account, symbol_code token_code, int64_t min_own_staked) {
    eosio_assert(0 <= min_own_staked, "min_own_staked can't be negative");
    modify_agent(account, token_code, [min_own_staked](auto& a) { a.min_own_staked = min_own_staked; } );
}

void stake::setkey(name account, symbol_code token_code, public_key signing_key) {
    modify_agent(account, token_code, [signing_key](auto& a) { a.signing_key = signing_key; } );
}

void stake::setproxylvl(name account, symbol_code token_code, uint8_t level) {
    require_auth(account);
    eosio::print("setproxylvl for ", account, " ", int(level), "\n");
    params params_table(table_owner, table_owner.value);
    const auto& param = get_param(params_table, symbol_code(0), token_code, false);
    
    eosio_assert(level <= param.max_proxies.size(), "level too high");
    agents agents_table(table_owner, table_owner.value);
    auto agents_idx = agents_table.get_index<"bykey"_n>();
    bool emplaced = false;
    auto agent = get_agent_itr(token_code, agents_idx, account, &param, &agents_table, &emplaced);
    eosio_assert(emplaced || (level != agent->proxy_level), "proxy level has not been changed");
    grants grants_table(table_owner, table_owner.value);
    auto grants_idx = grants_table.get_index<"bykey"_n>();
    uint8_t proxies_num = 0;
    auto grant_itr = grants_idx.lower_bound(std::make_tuple(token_code, account, name()));
    while ((grant_itr != grants_idx.end()) && (grant_itr->token_code == token_code) && (grant_itr->grantor_name == account)) { 
         ++proxies_num;
         ++grant_itr;
    }
    eosio_assert(level || !proxies_num, "can't set an ultimate level because the user has a proxy");
    eosio_assert(!level || proxies_num <= param.max_proxies[level - 1], "can't set proxy level, user has too many proxies");

    agents_idx.modify(agent, name(), [&](auto& a) { a.proxy_level = level; a.ultimate = !level; });
    
}

void stake::create(symbol token_symbol, std::vector<structures::param::purpose> purposes, std::vector<uint8_t> max_proxies, 
        int64_t frame_length, int64_t payout_step_lenght, uint16_t payout_steps_num)
{
    eosio::print("create stake for ", token_symbol.code(), "\n");
    eosio_assert(max_proxies.size(), "no proxy levels are specified");
    eosio_assert(max_proxies.size() < std::numeric_limits<uint8_t>::max(), "too many proxy levels");
    if (max_proxies.size() > 1)
        for (size_t i = 1; i < max_proxies.size(); i++) {
            eosio_assert(max_proxies[i - 1] >= max_proxies[i], "incorrect proxy levels");
        }
    auto issuer = eosio::token::get_issuer(config::token_name, token_symbol.code());
    require_auth(issuer);
    params params_table(table_owner, table_owner.value);
    eosio_assert(params_table.find(token_symbol.code().raw()) == params_table.end(), "already exists");
    
    params_table.emplace(issuer, [&](auto& p) { p = {
        .id = token_symbol.code().raw(),
        .token_symbol = token_symbol,
        .purposes = purposes,
        .max_proxies = max_proxies,
        .frame_length = frame_length
        };});
}

void stake::enable(symbol token_symbol, symbol_code purpose_code) {
    auto token_code = token_symbol.code();
    auto issuer = eosio::token::get_issuer(config::token_name, token_code);
    require_auth(issuer);
    params params_table(table_owner, table_owner.value);
    get_param(params_table, purpose_code, token_code);
    update_stats(structures::stat {
        .id = 0,
        .token_code = token_code,
        .purpose_code = purpose_code,
        .total_staked = 0,
        .enabled = true
    }, issuer);
}

stake::agents_idx_t::const_iterator stake::get_agent_itr(symbol_code token_code, stake::agents_idx_t& agents_idx, 
    name agent_name, const structures::param* param, agents* agents_table, bool* emplaced) {
        
    auto key = std::make_tuple(token_code, agent_name);
    auto agent = agents_idx.find(key);
    
    if (emplaced)
        *emplaced = false;

    if(!param) {
        eosio_assert(agent != agents_idx.end(), ("agent " + agent_name.to_string() + " doesn't exist").c_str());
    }
    else if(agent == agents_idx.end()) {

        eosio_assert(static_cast<bool>(agents_table), "SYSTEM: agents_table can't be null");
        (*agents_table).emplace(agent_name, [&](auto& a) { a = {
            .id = agents_table->available_primary_key(),
            .token_code = token_code,
            .account = agent_name,
            .proxy_level = static_cast<uint8_t>(param->max_proxies.size()),
            .ultimate = false,
            .last_proxied_update = time_point_sec(::now()),
            .purpose_funds = std::vector<structures::agent::purpose_funds_t>(param->purposes.size())
        };});
        
        agent = agents_idx.find(key);
        if (emplaced)
            *emplaced = true;
    }
    return agent;
}

void stake::updatefunds(name account, symbol_code token_code) {
    //require_auth(anyone);
    params params_table(table_owner, table_owner.value);
    const auto& param = get_param(params_table, symbol_code(0), token_code, false);
    update_stake_proxied(token_code, account, param.frame_length, false);
}

int64_t stake::update_purpose_balance(name issuer, agents_idx_t& agents_idx, agents_idx_t::const_iterator agent,
        symbol_code token_code, symbol_code purpose_code, size_t purpose_index, int64_t total_amount, int64_t total_balance) {
    if (!total_balance)
        return 0;

    auto amount = total_balance < 0 ? total_amount : safe_prop(total_amount, agent->purpose_funds[purpose_index].balance, total_balance);
    if (amount < 0)
        amount = std::max(amount, -agent->purpose_funds[purpose_index].balance);
    
    if (agent->purpose_funds[purpose_index].get_total_funds()) {
        agents_idx.modify(agent, name(), [&](auto& a) { a.purpose_funds[purpose_index].balance += amount; });
    }
    else {
        agents_idx.modify(agent, name(), [&](auto& a) {
            a.purpose_funds[purpose_index].balance = amount;
            a.purpose_funds[purpose_index].shares_sum = amount;
            a.purpose_funds[purpose_index].own_share = amount;
        });
    }
    
    update_stats(structures::stat {
        .id = 0,
        .token_code = token_code,
        .purpose_code = purpose_code,
        .total_staked = amount
    }, issuer);
    return amount;
}

void stake::change_balance(name account, asset quantity, symbol_code purpose_code) {
    eosio_assert(quantity.is_valid(), "invalid quantity");
    auto token_code = quantity.symbol.code();
    auto issuer = eosio::token::get_issuer(config::token_name, token_code);
    require_auth(issuer);
    params params_table(table_owner, table_owner.value);
    const auto& param = get_param(params_table, purpose_code, token_code, static_cast<bool>(purpose_code));

    agents agents_table(table_owner, table_owner.value);
    auto agents_idx = agents_table.get_index<"bykey"_n>();
    auto agent = get_agent_itr(token_code, agents_idx, account);
    
    int64_t actual_amount = 0;
    if (static_cast<bool>(purpose_code))
        actual_amount = update_purpose_balance(issuer, agents_idx, agent,
            token_code, purpose_code, param.get_purpose_index(purpose_code), quantity.amount);
    else {
        auto purposes_num = param.purposes.size();
        eosio_assert(purposes_num, "no purpose");
        int64_t total_balance = 0;
        for (size_t i = 0; i < purposes_num; i++) {
            total_balance += agent->purpose_funds[i].balance;
        }
        
        auto last_purpose_itr = param.purposes.end();
        --last_purpose_itr;
        auto total_amount = quantity.amount;
        auto left_amount = total_amount;
        size_t purpose_index = 0;
        for (auto purpose_itr = param.purposes.begin(); purpose_itr != last_purpose_itr; ++purpose_itr) {
            auto cur_actual_amount = update_purpose_balance(issuer, agents_idx, agent, 
                token_code, purpose_itr->code, purpose_index, total_amount, total_balance);
            left_amount -= cur_actual_amount;
            actual_amount += cur_actual_amount;
            purpose_index++;
        }
        actual_amount += update_purpose_balance(issuer, agents_idx, agent, 
            token_code, param.purposes.back().code, param.purposes.size() - 1, left_amount);
    }
    
    if (quantity.amount < 0) {
        quantity.amount = -actual_amount;
        INLINE_ACTION_SENDER(eosio::token, transfer)(config::token_name, {_self, config::active_name},
            {_self, issuer, quantity, ""});
        INLINE_ACTION_SENDER(eosio::token, retire)(config::token_name, {issuer, config::amerce_name}, {quantity, ""});
    }
    else {
        //we don't need actual_amount in this case
        INLINE_ACTION_SENDER(eosio::token, issue)(config::token_name, {issuer, config::reward_name}, {issuer, quantity, ""});
        INLINE_ACTION_SENDER(eosio::token, transfer)(config::token_name, {issuer, config::reward_name},
            {issuer, _self, quantity, config::reward_memo});    
    }
}

void stake::amerce(name account, asset quantity, symbol_code purpose_code) {
    eosio_assert(quantity.amount > 0, "quantity must be positive");
    eosio::print("stake::amerce: account = ", account, ", quantity = ", quantity, "\n");
    quantity.amount = -quantity.amount;
    change_balance(account, quantity, purpose_code);
}

void stake::reward(name account, asset quantity, symbol_code purpose_code) {
    eosio_assert(quantity.amount > 0, "quantity must be positive");
    eosio::print("stake::reward: account = ", account, ", quantity = ", quantity, "\n");
    change_balance(account, quantity, purpose_code);
}

} /// namespace cyber

DISPATCH_WITH_TRANSFER(cyber::stake, cyber::config::token_name, on_transfer,
    (create)(enable)(delegate)(setgrntterms)(recall)(withdraw)(claim)(cancelwd)
    (setproxylvl)(setproxyfee)(setminstaked)(setkey)
    (updatefunds)(amerce)(reward)
)
