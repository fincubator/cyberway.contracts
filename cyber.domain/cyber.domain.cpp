#include "cyber.domain.hpp"
#include <common/config.hpp>
#include <cyber.token/cyber.token.hpp>
#include <eosio/eosio.hpp>
#include <eosio/dispatcher.hpp>

#include "domain_validate.cpp"

namespace eosiosystem {


// constants
const uint32_t seconds_per_hour      = 60 * 60;
const uint32_t seconds_per_day       = 24 * seconds_per_hour;
const uint32_t seconds_per_year      = 365 * seconds_per_day;   // note: it's 52*7=364 in eos

// config
const uint32_t min_time_from_last_win = seconds_per_day;
const uint32_t min_time_from_last_bid = seconds_per_day;

using namespace eosio;

void validate_domain_name(const domain_name& n);
void validate_username(const username& n);


symbol core_symbol() {
    // const static auto sym = system_contract::get_core_symbol();  // requires system contract, which is not ready yet
    const static auto sym = cyber::config::system_token;
    return sym;
}

void domain::checkwin() {
    const auto now = eosio::current_time_point();

    auto state = state_singleton(_self, _self.value);
    auto s = state.get_or_create(_self, domain_bid_state{now});
    if ((now - s.last_win).to_seconds() > min_time_from_last_win) {
        domain_bid_tbl bids(_self, _self.value);
        auto idx = bids.get_index<"highbid"_n>();
        auto highest = idx.begin();
        if (highest != idx.end() &&
            highest->high_bid > 0 &&
            (now - highest->last_bid_time).to_seconds() > min_time_from_last_bid
        ) {
            s.last_win = now;
            state.set(s, _self);
            idx.modify(highest, same_payer, [&](auto& b) {
                b.high_bid = -b.high_bid;
            });
        }
    }
}


void domain::biddomain(name bidder, const domain_name& name, asset bid) {
    require_auth(bidder);
    checkwin();

    validate_domain_name(name);
    eosio::check(!is_domain(name), "domain already exists");
    eosio::check(bid.symbol == core_symbol(), "asset must be system token");
    eosio::check(bid.amount > 0, "insufficient bid");

    INLINE_ACTION_SENDER(eosio::token, transfer)(
        token_account, {{bidder, active_permission}},
        {bidder, names_account, bid, std::string("bid domain ") + name}
    );

    domain_bid_tbl bids(_self, _self.value);
    print(bidder, " bid ", bid, " on ", name, "\n");

    const auto set_bid = [&](auto& b) {
        b.high_bidder = bidder;
        b.high_bid = bid.amount;
        b.last_bid_time = eosio::current_time_point();
    };
    auto idx = bids.get_index<"domain"_n>();
    auto current = idx.find(name);
    if (current == idx.end()) {
        bids.emplace(bidder, [&](auto& b) {
            b.id = bids.available_primary_key();
            b.name = name;
            set_bid(b);
        });
    } else {
        eosio::check(current->high_bid > 0, "this auction has already closed");
        eosio::check((bid.amount - current->high_bid)*10 >= current->high_bid, "must increase bid by 10%");
        eosio::check(current->high_bidder != bidder, "account is already highest bidder");

        // the scope was newname.value, but we have string, so simplify.
        // downside: refund sum can be accumulated over several domains
        domain_bid_refund_tbl refunds(_self, _self.value);
        auto to_refund = asset(current->high_bid, core_symbol());
        auto itr = refunds.find(current->high_bidder.value);
        if (itr != refunds.end()) {
            refunds.modify(itr, same_payer, [&](auto& r) {
                r.amount += to_refund;
            });
        } else {
            refunds.emplace(bidder, [&](auto& r) {
                r.bidder = current->high_bidder;
                r.amount = to_refund;
            });
        }

        idx.modify(current, bidder, set_bid);
    }
}

void domain::biddmrefund(name bidder) {
    checkwin();
    domain_bid_refund_tbl refunds(_self, _self.value);
    auto itr = refunds.find(bidder.value);
    eosio::check(itr != refunds.end(), "Nothing to refund");
    INLINE_ACTION_SENDER(eosio::token, transfer)(
        token_account, {{names_account, active_permission}, {bidder, active_permission}},
        {names_account, bidder, asset(itr->amount), "refund bid on domain"}
    );
    refunds.erase(itr);
}

// name is already validated at this point
void domain_native::newdomain(name creator, const domain_name& name) {
    if (creator != _self) {
        auto dot = name.find('.');
        bool tld = dot == std::string::npos;    // Top Level Domain; TODO: decide what to do with SLD
        if (tld) {
            // auction
            domain_bid_tbl bids(_self, _self.value);
            auto idx = bids.get_index<"domain"_n>();
            auto bid = idx.find(name);
            eosio::check(bid != idx.end(), "no active bid for domain");
            eosio::check(bid->high_bidder == creator, "only highest bidder can claim");
            eosio::check(bid->high_bid < 0, "auction for domain is not closed yet");
            idx.erase(bid);
        } else {
            // only domain owner can create subdomains
            const domain_name suffix = name.substr(dot+1, name.size()-dot-1);
            eosio::check(is_domain(suffix), "parent domain do not exists");
            eosio::check(creator == get_domain_owner(suffix), "only owner of parent domain can create subdomain");
        }
    }
}

void domain::declarenames(const std::vector<name_info>& domains) {
    eosio::check(domains.size(), "domains must not be empty");
    name prev_account;
    domain_name prev_domain;
    for (const auto& info: domains) {
        const auto& domain = info.domain;
        const auto& dacc = info.account;
        if (domain.size() == 0 && prev_domain.size() == 0) {
            eosio::check(dacc > prev_account, ".account values must be ordered ascending, no repeats allowed");
            prev_account = dacc;
        } else {
            eosio::check(domain > prev_domain, ".domain values must be ordered ascending, no repeats allowed (except \"\")");
            prev_domain = domain;
            // TODO: it's handy to have domain name in assert messages
            validate_domain_name(domain);
            eosio::check(is_domain(domain), "domain doesn't exist");
            eosio::check(dacc == resolve_domain(domain), "domain resolves to different account");
        }
        for (const auto& u: info.users) {
            validate_username(u);
            eosio::check(is_username(dacc, u), "username doesn't exist in given scope");
        }
    }
}

} // eosiosystem


EOSIO_DISPATCH(eosiosystem::domain,
    (newdomain)(checkwin)(biddomain)(biddmrefund)(declarenames)
)
