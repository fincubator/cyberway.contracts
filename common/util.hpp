#pragma once
#include "config.hpp"

namespace cyber {


template<typename T>
struct member_pointer_info;

template<typename C, typename V>
struct member_pointer_info<V C::*> {
    using value_type = V;
    using class_type = C;
};

static int64_t safe_prop(int64_t arg, int64_t numer, int64_t denom) {
    return !arg || !numer ? 0 : static_cast<int64_t>((static_cast<int128_t>(arg) * numer) / denom);
}

static int64_t safe_pct(int64_t arg, int64_t total) {
    return safe_prop(arg, total,  config::_100percent);
}

static int64_t mul_cut(int64_t a, int64_t b) {
    static constexpr int128_t max_ret128 = std::numeric_limits<int64_t>::max();
    auto ret128 = static_cast<int128_t>(a) * b;
    return static_cast<int64_t>(std::min(max_ret128, ret128));
}

} // commun
