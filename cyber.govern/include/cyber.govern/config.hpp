#pragma once
#include <common/config.hpp>

namespace cyber { namespace config {
    
static const auto issuer_name = internal_name;
    
static const auto sum_up_interval = 701;
static const auto reward_from_funds_interval = 113;
static const auto update_emission_per_block_interval = 997;

static const auto balances_update_window = sum_up_interval * block_interval_ms / 1000;

static constexpr uint16_t active_producers_num = 21;
static constexpr uint16_t elected_producers_num = 25;
static constexpr uint16_t active_reserve_producers_num = 1;

static_assert(active_producers_num >= active_reserve_producers_num, "wrong producers num");
static constexpr uint16_t active_top_producers_num = active_producers_num - active_reserve_producers_num;

static constexpr auto emission_addition = 10 * _1percent;
static constexpr auto emission_factor   = 10 * _1percent;

static constexpr auto emission_min_arg = 25 * _1percent;
static constexpr auto emission_max_arg = 75 * _1percent;
//annual emission is equal to emission_addition                   if share of tokens voted <= emission_min_arg
//annual emission is equal to emission_addition + emission_factor if share of tokens voted >= emission_max_arg
//linear interpolation between these two points

static constexpr auto block_reward_pct     = 10 * _1percent;
static constexpr auto workers_reward_pct   = 20 * _1percent; // not including block reward //TODO: 22 * _1percent

}

} // cyber::config
