#pragma once
#include <eosio/eosio.hpp>
#include <eosio/time.hpp>
#include <eosio/singleton.hpp>
#include <tuple>
#include <eosio/privileged.hpp>
#include <cyber.token/cyber.token.hpp>
#include <common/dispatchers.hpp>

namespace cyber {
using eosio::name;
class [[eosio::contract("cyber.box")]] box : public eosio::contract {
struct structures {
    struct box {
        uint64_t id;
        name contract;
        name treasurer;
        name title;
        name owner;
        bool empty;
        uint64_t primary_key()const { return id; }
        using key_t = std::tuple<name, name, name>;
        key_t by_key()const { return std::make_tuple(contract, treasurer, title); }
    };
};
    using box_key_index [[using eosio: order("contract"), order("treasurer"), order("title")]] =
        eosio::indexed_by<"bykey"_n, eosio::const_mem_fun<structures::box, structures::box::key_t, &structures::box::by_key> >;
    using boxes [[eosio::order("id")]] = eosio::multi_index<"box"_n, structures::box, box_key_index>;

    void erase_box(name contract, name treasurer, name title, bool release);
public:
    using contract::contract;
    [[eosio::action]] void create(name contract, name treasurer, name title);
    [[eosio::action]] void packup(name contract, name treasurer, name title);
    [[eosio::action]] void unpack(name contract, name treasurer, name title);
    [[eosio::action]] void burn(name contract, name treasurer, name title);
    [[eosio::action]] void transfer(name contract, name treasurer, name title, name to, std::string memo);
    //do we need to add the ability to put boxes in a box?
};
} /// namespace cyber
