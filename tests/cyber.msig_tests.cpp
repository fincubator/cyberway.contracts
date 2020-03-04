#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/wast_to_wasm.hpp>

#include <Runtime/Runtime.h>

#include <fc/variant_object.hpp>
#include "contracts.hpp"
#include "test_symbol.hpp"

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

class cyber_msig_tester : public tester {
public:
   cyber_msig_tester() {
      create_accounts({config::ram_account_name, config::ramfee_account_name,
         N(alice), N(bob), N(carol)});
      produce_block();

      const auto sys_priv_key = get_private_key(config::system_account_name, name{config::active_name}.to_string());
      set_code(config::msig_account_name, contracts::msig_wasm(), &sys_priv_key);
      set_abi(config::msig_account_name, contracts::msig_abi().data(), &sys_priv_key);

      produce_blocks();
      const auto& accnt = control->chaindb().get<account_object>(config::msig_account_name);
      abi_def abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      abi_ser.set_abi(abi, abi_serializer_max_time);
   }

   transaction_trace_ptr create_account_with_resources( account_name a, account_name creator, asset ramfunds, bool multisig,
                                                        asset net = core_sym::from_string("10.0000"), asset cpu = core_sym::from_string("10.0000") ) {
      signed_transaction trx;
      set_transaction_headers(trx);

      authority owner_auth;
      if (multisig) {
         // multisig between account's owner key and creators active permission
         owner_auth = authority(2, {key_weight{get_public_key( a, "owner" ), 1}}, {permission_level_weight{{creator, config::active_name}, 1}});
      } else {
         owner_auth =  authority( get_public_key( a, "owner" ) );
      }

      trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                                newaccount{
                                   .creator  = creator,
                                   .name     = a,
                                   .owner    = owner_auth,
                                   .active   = authority( get_public_key( a, "active" ) )
                                });

      trx.actions.emplace_back( get_action(config::system_account_name, N(buyram), vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("payer", creator)
                                            ("receiver", a)
                                            ("quant", ramfunds) )
                              );

      trx.actions.emplace_back( get_action(config::system_account_name, N(delegatebw), vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("from", creator)
                                            ("receiver", a)
                                            ("stake_net_quantity", net )
                                            ("stake_cpu_quantity", cpu )
                                            ("transfer", 0 )
                                          )
                                );

      set_transaction_headers(trx);
      trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );
      return push_transaction( trx );
   }
   void create_currency( name contract, name manager, asset maxsupply ) {
      auto act =  mutable_variant_object()
         ("issuer",       manager )
         ("maximum_supply", maxsupply );

      base_tester::push_action(contract, N(create), contract, act );
   }
   void issue( name to, const asset& amount, name manager = config::system_account_name ) {
      base_tester::push_action(config::token_account_name, N(issue), manager, mutable_variant_object()
                                ("to",      to )
                                ("quantity", amount )
                                ("memo", "")
                                );
   }
   void transfer( name from, name to, const string& amount, name manager = config::system_account_name ) {
      base_tester::push_action(config::token_account_name, N(transfer), manager, mutable_variant_object()
                                ("from",    from)
                                ("to",      to )
                                ("quantity", asset::from_string(amount) )
                                ("memo", "")
                                );
   }
   asset get_balance( const account_name& act ) {
      return tester::get_currency_balance(config::token_account_name, symbol(CORE_SYM), act);
   }

   transaction_trace_ptr push_action( const account_name& signer, const action_name& name, const variant_object& data, bool auth = true ) {
      vector<account_name> accounts;
      if( auth )
         accounts.push_back( signer );
      auto trace = base_tester::push_action(config::msig_account_name, name, accounts, data );
      produce_block();
      BOOST_REQUIRE_EQUAL( true, chain_has_transaction(trace->id) );
      return trace;

      /*
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = config::msig_account_name;
         act.name = name;
         act.data = abi_ser.variant_to_binary( action_type_name, data, abi_serializer_max_time );
         //std::cout << "test:\n" << fc::to_hex(act.data.data(), act.data.size()) << " size = " << act.data.size() << std::endl;

         return base_tester::push_action( std::move(act), auth ? uint64_t(signer) : 0 );
      */
   }

   auto push_action(name code, name signer, name action, const mvo& data, bool add_nested = false) {
      vector<permission_level> auths{{signer, N(active)}};
      auto trace = base_tester::push_action(code, action, auths, data, base_tester::DEFAULT_EXPIRATION_DELTA, 0, add_nested);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));
      return trace;
   }

   transaction reqauth( account_name from, const vector<permission_level>& auths, const fc::microseconds& max_serialization_time );
   transaction reqauth_delayed(
      account_name from, const vector<permission_level>& auths, const uint32_t delay, const fc::variants& add_actions = {});

   abi_serializer abi_ser;

   // api // can be moved to test_api when decide to refactor tests (leads to manual merge of upstream changes)
   transaction_trace_ptr propose(
      name proposer, name proposal, const transaction& trx, const vector<permission_level>& requested,
      optional<string> description = {});
   transaction_trace_ptr approve(name proposer, name proposal, const permission_level& level);
   transaction_trace_ptr schedule(name proposer, name proposal, name actor);
   transaction_trace_ptr exec(name proposer, name proposal, name executer);
};

transaction cyber_msig_tester::reqauth( account_name from, const vector<permission_level>& auths, const fc::microseconds& max_serialization_time ) {
   fc::variants v;
   for ( auto& level : auths ) {
      v.push_back(fc::mutable_variant_object()
                  ("actor", level.actor)
                  ("permission", level.permission)
      );
   }
   variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("max_ram_kbytes", 0)
      ("max_storage_kbytes", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "reqauth")
               ("authorization", v)
               ("data", fc::mutable_variant_object() ("from", from) )
               })
      );
   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), max_serialization_time);
   return trx;
}

transaction cyber_msig_tester::reqauth_delayed(
   account_name from, const vector<permission_level>& auths, const uint32_t delay, const fc::variants& add_actions
) {
   fc::variants v;
   for (auto& level: auths) {
      v.push_back(mvo("actor", level.actor)("permission", level.permission));
   }
   fc::variants actions{add_actions};
   actions.push_back(mvo
      ("account", name{config::system_account_name})
      ("name", "reqauth")
      ("authorization", v)
      ("data", mvo("from", from))
   );
   variant pretty_trx = mvo
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 0)
      ("ref_block_prefix", 0)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("max_ram_kbytes", 0)
      ("max_storage_kbytes", 0)
      ("delay_sec", delay)
      ("actions", actions);
   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);
   return trx;
}


transaction_trace_ptr cyber_msig_tester::propose(
   name proposer, name proposal, const transaction& trx, const vector<permission_level>& requested, optional<string> description
) {
   auto args = mvo
      ("proposer", proposer)
      ("proposal_name", proposal)
      ("trx", trx)
      ("requested", requested);
   if (description) {
      args = args("description", *description);
   }
   return push_action(proposer, N(propose), args);
}

transaction_trace_ptr cyber_msig_tester::approve(name proposer, name proposal, const permission_level& level) {
   return push_action(level.actor, N(approve), mvo
      ("proposer", proposer)
      ("proposal_name", proposal)
      ("level", level)
   );
}

transaction_trace_ptr cyber_msig_tester::schedule(name proposer, name proposal, name actor) {
   return push_action(actor, N(schedule), mvo
      ("proposer", proposer)
      ("proposal_name", proposal)
      ("actor", actor)
   );
}

transaction_trace_ptr cyber_msig_tester::exec(name proposer, name proposal, name executer) {
   return push_action(config::msig_account_name, executer, N(exec), mvo
      ("proposer", proposer)
      ("proposal_name", proposal)
      ("executer", executer),
      true
   );
}


BOOST_AUTO_TEST_SUITE(cyber_msig_tests)

BOOST_FIXTURE_TEST_CASE( propose_approve_execute, cyber_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION(push_action(config::msig_account_name, N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                                          , true
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //approve and execute
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect([&]( const transaction_trace_ptr& t) { if (t->nested) { trace = t; } } );
   push_action(config::msig_account_name, N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
                  , true
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_TEST_MESSAGE("TRACE : " << fc::json::to_string(*trace));
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_approve_unapprove, cyber_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   push_action( N(alice), N(unapprove), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   BOOST_REQUIRE_EXCEPTION( push_action(config::msig_account_name, N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                                          , true
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_approve_by_two, cyber_msig_tester ) try {
   auto trx = reqauth("alice", vector<permission_level>{ { N(alice), config::active_name }, { N(bob), config::active_name } }, abi_serializer_max_time );
   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{ { N(alice), config::active_name }, { N(bob), config::active_name } })
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   //fail because approval by bob is missing

   BOOST_REQUIRE_EXCEPTION( push_action(config::msig_account_name, N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                                          , true
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //approve by bob and execute
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect([&]( const transaction_trace_ptr& t) { if (t->nested) { trace = t; } } );

   push_action(config::msig_account_name, N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
                  , true
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_with_wrong_requested_auth, cyber_msig_tester ) try {
   auto trx = reqauth("alice", vector<permission_level>{ { N(alice), config::active_name },  { N(bob), config::active_name } }, abi_serializer_max_time );
   //try with not enough requested auth
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(propose), mvo()
                                             ("proposer",      "alice")
                                             ("proposal_name", "third")
                                             ("trx",           trx)
                                             ("requested", vector<permission_level>{ { N(alice), config::active_name } } )
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( big_transaction, cyber_msig_tester ) try {
   vector<permission_level> perm = { { N(alice), config::active_name }, { N(bob), config::active_name } };
   auto wasm = contracts::util::exchange_wasm();

   variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("max_ram_kbytes", 0)
      ("max_storage_kbytes", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", perm)
               ("data", fc::mutable_variant_object()
                ("account", "alice")
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer_max_time);

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );
   //approve by bob and execute
   push_action( N(bob), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(bob), config::active_name })
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect([&]( const transaction_trace_ptr& t) { if (t->nested) { trace = t; } } );

   BOOST_REQUIRE_THROW(push_action(config::msig_account_name, N(alice), N(exec), mvo("proposer",      "alice")
                                                       ("proposal_name", "first")
                                                       ("executer",      "alice"), true),
                       fc::exception);

   // TODO: Cyberway exchange_wasm is compiled for EOS
   return;

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( propose_approve_invalidate, cyber_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION(push_action(config::msig_account_name, N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                                          , true
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //approve
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   //invalidate
   push_action( N(alice), N(invalidate), mvo()
                  ("account",      "alice")
   );

   //fail to execute after invalidation
   BOOST_REQUIRE_EXCEPTION(push_action(config::msig_account_name, N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                                          , true
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( propose_invalidate_approve, cyber_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action(config::msig_account_name, N(alice), N(exec), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                                          , true
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //invalidate
   push_action( N(alice), N(invalidate), mvo()
                  ("account",      "alice")
   );

   //approve
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
   );

   //successfully execute
   transaction_trace_ptr trace;
   control->applied_transaction.connect([&]( const transaction_trace_ptr& t) { if (t->nested) { trace = t; } } );

   push_action(config::msig_account_name, N(bob), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "bob")
                  , true
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( approve_with_hash, cyber_msig_tester ) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );
   auto trx_hash = fc::sha256::hash( trx );
   auto not_trx_hash = fc::sha256::hash( trx_hash );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   //fail to approve with incorrect hash
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(approve), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("level",         permission_level{ N(alice), config::active_name })
                                          ("proposal_hash", not_trx_hash)
                            ),
                            eosio::chain::crypto_api_exception,
                            fc_exception_message_is("hash mismatch")
   );

   //approve and execute
   push_action( N(alice), N(approve), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ N(alice), config::active_name })
                  ("proposal_hash", trx_hash)
   );

   transaction_trace_ptr trace;
   control->applied_transaction.connect([&]( const transaction_trace_ptr& t) { if (t->nested) { trace = t; } } );
   push_action(config::msig_account_name, N(alice), N(exec), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("executer",      "alice")
                  , true
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( switch_proposal_and_fail_approve_with_hash, cyber_msig_tester ) try {
   auto trx1 = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time );
   auto trx1_hash = fc::sha256::hash( trx1 );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx1)
                  ("requested", vector<permission_level>{{ N(alice), config::active_name }})
   );

   auto trx2 = reqauth("alice",
                       { permission_level{N(alice), config::active_name},
                         permission_level{N(alice), config::owner_name}  },
                       abi_serializer_max_time );

   push_action( N(alice), N(cancel), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("canceler",       "alice")
   );

   push_action( N(alice), N(propose), mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx2)
                  ("requested", vector<permission_level>{ { N(alice), config::active_name },
                                                          { N(alice), config::owner_name } })
   );

   //fail to approve with hash meant for old proposal
   BOOST_REQUIRE_EXCEPTION( push_action( N(alice), N(approve), mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("level",         permission_level{ N(alice), config::active_name })
                                          ("proposal_hash", trx1_hash)
                            ),
                            eosio::chain::crypto_api_exception,
                            fc_exception_message_is("hash mismatch")
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(propose_with_description, cyber_msig_tester) try {
   auto trx = reqauth("alice", {permission_level{N(alice), config::active_name}}, abi_serializer_max_time);
   const name alice = N(alice);
   const name proposal = N(description);
   const permission_level alice_perm{N(alice), config::active_name};

   propose(alice, proposal, trx, {alice_perm}, "Propose to Hello world!");
   approve(alice, proposal, alice_perm);

   transaction_trace_ptr trace;
   control->applied_transaction.connect([&](const transaction_trace_ptr& t) { if (t->nested) { trace = t; } });
   exec(alice, proposal, alice);

   BOOST_REQUIRE(bool(trace));
   BOOST_REQUIRE_EQUAL(1, trace->action_traces.size());
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(auth_wait, cyber_msig_tester) try {
   BOOST_TEST_MESSAGE("Schedule (auth with waits)");
   const name proposal = N(wait.auth);
   const name active = config::active_name;
   const name bps = N(bps);
   const name alice = N(alice);
   const name carol = N(carol);
   const name bob = N(bob);
   const permission_level alice_perm{alice, active};
   const permission_level carol_perm{carol, active};
   const permission_level bob_perm{bob, active};
   const permission_level bps_perm{bps, active};
   const int short_delay = 60;
   const int long_delay = short_delay * 3;

   signed_transaction trx_acc;
   authority auth = authority(3, {},
      {{alice_perm, 1}, {bob_perm, 1}, {carol_perm, 1}},
      {{short_delay, 1}, {long_delay, 1}} // waits are cumulative
   );
   trx_acc.actions.emplace_back(vector<permission_level>{bob_perm}, newaccount{
      .creator = bob,
      .name    = bps,
      .active  = auth,
      .owner   = authority(get_public_key(bps, "owner"))
   });
   set_transaction_headers(trx_acc);
   trx_acc.sign(get_private_key(bob, "active"), control->get_chain_id());
   auto trace = push_transaction(trx_acc);

   const auto check_exec = [&](name proposer, name proposal) {
      transaction_trace_ptr trace;
      control->applied_transaction.connect([&](const transaction_trace_ptr& t) { if (t->nested) { trace = t; } });
      exec(proposer, proposal, proposer);
      BOOST_REQUIRE(bool(trace));
      BOOST_REQUIRE_EQUAL(1, trace->action_traces.size());
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
   };
   auto err_auth = eosio_assert_message_is{"transaction authorization failed"};
   auto too_early = eosio_assert_message_is{"too early"};

   BOOST_TEST_MESSAGE("--- propose with long delay");
   const vector<permission_level> all_perms{alice_perm, bob_perm, carol_perm};
   auto trx = reqauth_delayed(bps, {bps_perm}, long_delay);
   propose(bob, proposal, trx, all_perms);
   BOOST_TEST_MESSAGE("----- fail to `schedule` when no approvals");
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   BOOST_REQUIRE_EXCEPTION(schedule(bob, proposal, bob), eosio_assert_message_exception, err_auth);
   BOOST_TEST_MESSAGE("----- successful `schedule` when one approval + enough delay");
   approve(bob, proposal, bob_perm);
   produce_block();
   schedule(bob, proposal, bob);
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   BOOST_TEST_MESSAGE("----- wait one block before ready and check it's failing to exec");
   int wait_blocks = long_delay / 3 - 1 - 1; // -1 = 1 block (within `schedule` call)
   produce_blocks(wait_blocks);
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   BOOST_TEST_MESSAGE("----- successful exec after waiting one more block");
   produce_block();
   check_exec(bob, proposal);

   BOOST_TEST_MESSAGE("--- propose with short delay, can't execute earlier even with all approvals");
   trx = reqauth_delayed(bps, {bps_perm}, short_delay);
   propose(bob, proposal, trx, all_perms);
   BOOST_TEST_MESSAGE("----- fail to `schedule` with one approval");
   approve(bob, proposal, bob_perm);
   BOOST_REQUIRE_EXCEPTION(schedule(bob, proposal, bob), eosio_assert_message_exception, err_auth);
   BOOST_TEST_MESSAGE("----- fail to `exec` without `schedule` when have all approvals");
   approve(bob, proposal, alice_perm);
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   approve(bob, proposal, carol_perm);
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   BOOST_TEST_MESSAGE("----- still fail to `exec` one block earlier than delay");
   schedule(bob, proposal, bob);
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   wait_blocks = short_delay / 3 - 1 - 1; // -1 = 1 block (within `schedule` call)
   produce_blocks(wait_blocks);
   BOOST_REQUIRE_EXCEPTION(check_exec(bob, proposal), eosio_assert_message_exception, too_early);
   BOOST_TEST_MESSAGE("----- successful `exec` after delay");
   produce_block();
   check_exec(bob, proposal);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(no_delay_schedule, cyber_msig_tester) try {
   BOOST_TEST_MESSAGE("Schedule with 0 delay");
   const name proposal = N(no.delay);
   const name bob = N(bob);
   const permission_level bob_perm{bob, config::active_name};

   auto trx = reqauth(bob, {bob_perm}, abi_serializer_max_time);
   propose(bob, proposal, trx, {bob_perm});
   BOOST_REQUIRE_EXCEPTION(schedule(bob, proposal, bob), eosio_assert_message_exception,
      eosio_assert_message_is("can't schedule transaction with zero delay_sec, call exec"));

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
