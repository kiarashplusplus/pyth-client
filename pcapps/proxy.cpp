#include "proxy.hpp"
#include <pc/log.hpp>

#define PC_TPU_PROXY_PORT     8898
#define PC_RPC_HTTP_PORT      8899
#define PC_RPC_WEBSOCKET_PORT 8900
#define PC_RECONNECT_TIMEOUT  (120L*1000000000L)

using namespace pc;

///////////////////////////////////////////////////////////////////////////
// proxy_user

proxy_user::proxy_user()
{
  set_net_parser( this );
}

void proxy_user::set_proxy( proxy *mgr )
{
  mgr_ = mgr;
}

bool proxy_user::parse( const char *buf, size_t sz, size_t&len  )
{
  tpu_hdr *hdr = (tpu_hdr*)buf;
  if ( PC_UNLIKELY( sz < sizeof( tpu_hdr) || sz < hdr->size_ ) ) {
    return false;
  }
  if ( PC_UNLIKELY( hdr->proto_id_ != PC_TPU_PROTO_ID ) ) {
    teardown();
    return false;
  }
  mgr_->submit( (const char*)&hdr[1], hdr->size_ - sizeof( tpu_hdr ) );
  len = hdr->size_;
  return true;
}

void proxy_user::teardown()
{
  net_connect::teardown();

  // remove self from server list
  mgr_->del_user( this );
}

///////////////////////////////////////////////////////////////////////////
// proxy

proxy::proxy()
: has_curr_( false ),
  has_next_( false ),
  has_conn_( false ),
  wait_conn_( false ),
  msg_( new char[buf_len] ),
  slot_( 0UL ),
  cts_( 0L ),
  ctimeout_( PC_NSECS_IN_SEC )
{
  sreq_->set_sub( this );
  creq_->set_sub( this );
  lreq_->set_sub( this );
//  lreq_->set_limit( 256 );
  lreq_->set_limit( 32 );
}

proxy::~proxy()
{
  teardown();
}

void proxy::set_rpc_host( const std::string& rhost )
{
  rhost_ = rhost;
}

std::string proxy::get_rpc_host() const
{
  return rhost_;
}

void proxy::set_listen_port( int port )
{
  tsvr_.set_port( port );
}

int proxy::get_listen_port() const
{
  return tsvr_.get_port();
}

bool proxy::init()
{
  // initialize net_loop
  if ( !nl_.init() ) {
    return set_err_msg( nl_.get_err_msg() );
  }
  // add rpc_client connection to net_loop and initialize
  hconn_.set_port( PC_RPC_HTTP_PORT );
  hconn_.set_host( rhost_ );
  hconn_.set_net_loop( &nl_ );
  clnt_.set_http_conn( &hconn_ );
  wconn_.set_port( PC_RPC_WEBSOCKET_PORT );
  wconn_.set_host( rhost_ );
  wconn_.set_net_loop( &nl_ );
  clnt_.set_ws_conn( &wconn_ );
  if ( !hconn_.init() ) {
    return set_err_msg( hconn_.get_err_msg() );
  }
  if ( !wconn_.init() ) {
    return set_err_msg( wconn_.get_err_msg() );
  }
  if ( !tconn_.init() ) {
    return set_err_msg( tconn_.get_err_msg() );
  }
  tsvr_.set_port(PC_TPU_PROXY_PORT );
  tsvr_.set_net_accept( this );
  tsvr_.set_net_loop( &nl_ );
  if ( !tsvr_.init() ) {
    return set_err_msg( tsvr_.get_err_msg() );
  }
  PC_LOG_INF("listening").add("port",tsvr_.get_port()).end();
  wait_conn_ = true;
  return true;
}

void proxy::poll()
{
  // epoll loop
  nl_.poll( 1 );

  // destroy any users scheduled for deletion
  teardown_users();

  // reconnect to rpc as required
  if ( PC_UNLIKELY( !has_conn_ ||
        hconn_.get_is_err() || wconn_.get_is_err() ) ) {
    reconnect_rpc();
  }
}

void proxy::del_user( proxy_user *usr )
{
  // move usr from open clist to delete list
  olist_.del( usr );
  dlist_.add( usr );
}

void proxy::teardown_users()
{
  while( !dlist_.empty() ) {
    proxy_user *usr = dlist_.first();
    PC_LOG_DBG( "delete_user" ).add("fd", usr->get_fd() ).end();
    usr->close();
    dlist_.del( usr );
  }
}

void proxy::accept( int fd )
{
  // create and add new user
  proxy_user *usr = new proxy_user;
  usr->set_net_loop( &nl_ );
  usr->set_proxy( this );
  usr->set_fd( fd );
  usr->set_block( false );
  if ( usr->init() ) {
    PC_LOG_DBG( "new_user" ).add("fd", fd ).end();
    olist_.add( usr );
  } else {
    usr->close();
    delete usr;
  }
}

void proxy::submit( const char *buf, size_t len )
{
  // send to current leader
  if ( has_curr_ ) {
    tconn_.send( curr_ldr_, buf, len );
  }

  // send to next leader (if not same as current leader)
  if ( has_next_ ) {
    tconn_.send( next_ldr_, buf, len );
  }
}

void proxy::on_response( rpc::slot_subscribe *res )
{
  // ignore slots that go back in time
  uint64_t slot = res->get_slot();
  if ( slot <= slot_ ) {
    return;
  }
  slot_ = slot;
  PC_LOG_DBG( "receive slot" ).add( "slot", slot_ ).end();

  // request next slot leader schedule
  if ( PC_UNLIKELY( lreq_->get_is_recv() &&
                    slot_ > lreq_->get_last_slot() - 16 ) ) {
    lreq_->set_slot( slot_ );
    clnt_.send( lreq_ );
  }

  // update ip address of current and next leader
  pub_key *pkey = lreq_->get_leader( slot_ );
  has_curr_ = pkey && creq_->get_ip_addr( *pkey, *curr_ldr_ );
  pub_key *nkey = lreq_->get_leader( slot_+1 );
  has_next_ = nkey && *nkey != *pkey &&
    creq_->get_ip_addr( *nkey, *next_ldr_ );
  if ( has_curr_ ) {
    PC_LOG_DBG( "current leader" ).add( "key", *pkey ).end();
  }
  if ( has_next_ ) {
    PC_LOG_DBG( "next leader" ).add( "key", *nkey ).end();
  }
}

void proxy::on_response( rpc::get_cluster_nodes *m )
{
  if ( m->get_is_err() ) {
    set_err_msg( "failed to get cluster nodes["
        + m->get_err_msg()  + "]" );
    return;
  }
  PC_LOG_DBG( "received get_cluster_nodes" ).end();
}

void proxy::on_response( rpc::get_slot_leaders *m )
{
  if ( m->get_is_err() ) {
    set_err_msg( "failed to get slot leaders ["
        + m->get_err_msg()  + "]" );
    return;
  }
  PC_LOG_DBG( "received get_slot_leaders" ).end();
}

void proxy::reconnect_rpc()
{
  // check if connection process has complete
  if ( hconn_.get_is_wait() ) {
    hconn_.check();
  }
  if ( wconn_.get_is_wait() ) {
    wconn_.check();
  }
  if ( hconn_.get_is_wait() || wconn_.get_is_wait() ) {
    return;
  }

  // check for successful (re)connect
  if ( !hconn_.get_is_err() && !wconn_.get_is_err() ) {
    PC_LOG_INF( "rpc_connected" ).end();

    // reset state
    has_conn_  = true;
    wait_conn_ = false;
    slot_ = 0L;
    clnt_.reset();

    // subscribe to slots and cluster addresses
    clnt_.send( sreq_ );
    clnt_.send( creq_ );
    return;
  }

  // log disconnect error
  if ( wait_conn_ || has_conn_ ) {
    wait_conn_ = false;
    log_disconnect();
  }

  // wait for reconnect timeout
  has_conn_ = false;
  int64_t ts = get_now();
  if ( ctimeout_ > (ts-cts_) ) {
    return;
  }

  // attempt to reconnect
  cts_ = ts;
  ctimeout_ += ctimeout_;
  ctimeout_ = std::min( ctimeout_, PC_RECONNECT_TIMEOUT );
  wait_conn_ = true;
  hconn_.init();
  wconn_.init();
}

void proxy::log_disconnect()
{
  if ( hconn_.get_is_err() ) {
    PC_LOG_ERR( "rpc_http_reset")
      .add( "error", hconn_.get_err_msg() )
      .add( "host", rhost_ )
      .add( "port", hconn_.get_port() )
      .end();
    return;
  }
  if ( wconn_.get_is_err() ) {
    PC_LOG_ERR( "rpc_websocket_reset" )
      .add( "error", wconn_.get_err_msg() )
      .add( "host", rhost_ )
      .add( "port", wconn_.get_port() )
      .end();
    return;
  }
}

void proxy::teardown()
{
  PC_LOG_INF( "pyth_proxy_teardown" ).end();

  // shutdown listener
  tsvr_.close();

  // destroy any open users
  while( !olist_.empty() ) {
    proxy_user *usr = olist_.first();
    olist_.del( usr );
    dlist_.add( usr );
  }
  teardown_users();

  // destroy rpc connections
  hconn_.close();
  wconn_.close();
}
