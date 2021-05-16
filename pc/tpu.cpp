#include "tpu.hpp"
#include "bincode.hpp"
#include "manager.hpp"

using namespace pc;

///////////////////////////////////////////////////////////////////////////
// tpu_price

tpu_price::tpu_price()
: cmd_( e_cmd_upd_price )
{
}

void tpu_price::set_symbol_status( symbol_status st )
{
  st_ = st;
}

void tpu_price::set_publish( key_pair *pk )
{
  pkey_ = pk;
}

void tpu_price::set_account( pub_key *akey )
{
  akey_ = akey;
}

void tpu_price::set_program( pub_key *gkey )
{
  gkey_ = gkey;
}

void tpu_price::set_block_hash( hash *bhash )
{
  bhash_ = bhash;
}

void tpu_price::set_price( int64_t px,
                           uint64_t conf,
                           symbol_status st,
                           uint64_t pub_slot,
                           bool is_agg )
{
  price_ = px;
  conf_  = conf;
  st_    = st;
  pub_slot_ = pub_slot;
  cmd_   = is_agg?e_cmd_agg_price:e_cmd_upd_price;
}

void tpu_price::build( net_buf *bptr )
{
  bincode tx( bptr->buf_ );

  // signatures section
  tx.add_len<1>();      // one signature (publish)
  size_t pub_idx = tx.reserve_sign();

  // message header
  size_t tx_idx = tx.get_pos();
  tx.add( (uint8_t)1 ); // pub is only signing account
  tx.add( (uint8_t)0 ); // read-only signed accounts
  tx.add( (uint8_t)2 ); // sysvar and program-id are read-only
                        // unsigned accounts

  // accounts
  tx.add_len<4>();      // 3 accounts: publish, symbol, sysvar, program
  tx.add( *pkey_ );     // publish account
  tx.add( *akey_ );     // symbol account
  tx.add( *(pub_key*)sysvar_clock );
  tx.add( *gkey_ );     // programid

  // recent block hash
  tx.add( *bhash_ );    // recent block hash

  // instructions section
  tx.add_len<1>();      // one instruction
  tx.add( (uint8_t)3);  // program_id index
  tx.add_len<3>();      // 3 accounts: publish, symbol
  tx.add( (uint8_t)0 ); // index of publish account
  tx.add( (uint8_t)1 ); // index of symbol account
  tx.add( (uint8_t)2 ); // index of sysvar account

  // instruction parameter section
  tx.add_len<sizeof(cmd_upd_price)>();
  tx.add( (uint32_t)PC_VERSION );
  tx.add( (int32_t)cmd_ );
  tx.add( (int32_t)st_ );
  tx.add( (int32_t)0 );
  tx.add( price_ );
  tx.add( conf_ );
  tx.add( pub_slot_ );

  // all accounts need to sign transaction
  tx.sign( pub_idx, tx_idx, *pkey_ );
  bptr->size_ = tx.size();
}

///////////////////////////////////////////////////////////////////////////
// tpu_pub

tpu_pub::tpu_pub()
: has_curr_( false ),
  has_next_( false ),
  clnt_( nullptr ),
  slot_( 0UL )
{
  creq_->set_sub( this );
  lreq_->set_sub( this );
  lreq_->set_limit( 256 );
}

bool tpu_pub::init()
{
  // initialize udp connection
  if ( !tconn_.init() ) {
    return set_err_msg( tconn_.get_err_msg() );
  }
  // get cluster leadership nodes
  clnt_->send( creq_ );
  return true;
}

void tpu_pub::next_slot( uint64_t slot )
{
  // update current slot
  slot_ = slot;

  // request next slot leader schedule
  if ( PC_UNLIKELY( lreq_->get_is_recv() &&
                    slot_ > lreq_->get_last_slot() - 16 ) ) {
    lreq_->set_slot( slot_ );
    clnt_->send( lreq_ );
  }

  // update ip address of current and next leader
  pub_key *pkey = lreq_->get_leader( slot_ );
  has_curr_ = pkey && creq_->get_ip_addr( *pkey, *curr_ldr_ );
  pub_key *nkey = lreq_->get_leader( slot_+1 );
  has_next_ = nkey && *nkey != *pkey &&
    creq_->get_ip_addr( *nkey, *next_ldr_ );
}

void tpu_pub::submit( tpu_request *buf )
{
  // build transaction
  net_buf *bptr = net_buf::alloc();
  buf->build( bptr );

  // send to current leader
  if ( has_curr_ ) {
    tconn_.send( curr_ldr_, bptr );
  }

  // send to next leader (if not same as current leader)
  if ( has_next_ ) {
    tconn_.send( next_ldr_, bptr );
  }

  // dealloc buffer
  bptr->dealloc();
}

///////////////////////////////////////////////////////////////////////////
// tpu_embed

tpu_embed::tpu_embed()
:   mgr_( nullptr )
{
}

void tpu_embed::set_manager ( manager *mgr )
{
  mgr_ = mgr;
  set_rpc_client( mgr_->get_rpc_client() );
}

manager *tpu_embed::get_manager() const
{
  return mgr_;
}

void tpu_embed::poll()
{
  // get next leader slot as required
  uint64_t slot = mgr_->get_slot();
  if ( PC_UNLIKELY( slot_ != slot ))  {
    next_slot( slot );
  }
}

///////////////////////////////////////////////////////////////////////////
// tpu_proxy


