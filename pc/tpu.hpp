#pragma once

#include <pc/net_socket.hpp>
#include <pc/rpc_client.hpp>

namespace pc
{

  // transaction builder
  class tpu_request : public error
  {
  public:
    virtual ~tpu_request();
    virtual void build( net_buf * ) = 0;
  };

  // set new component price
  class tpu_price : public tpu_request
  {
  public:
    tpu_price();
    void set_symbol_status( symbol_status );
    void set_publish( key_pair * );
    void set_account( pub_key * );
    void set_program( pub_key * );
    void set_block_hash( hash * );
    void set_price( int64_t px, uint64_t conf, symbol_status,
                    uint64_t pub_slot, bool is_aggregate );
    void build( net_buf* ) override;

  private:
    hash         *bhash_;
    key_pair     *pkey_;
    pub_key      *gkey_;
    pub_key      *akey_;
    int64_t       price_;
    uint64_t      conf_;
    uint64_t      pub_slot_;;
    command_t     cmd_;
    symbol_status st_;
  };

  // transaction submission api
  class tpu : public error
  {
  public:
    virtual ~tpu();
    virtual bool init();
    virtual void poll();
    virtual void submit( tpu_request * );
  };

  class manager;

  // tpu publisher impl base class
  class tpu_pub : public tpu,
                  public rpc_sub,
                  public rpc_sub_i<rpc::get_cluster_nodes>,
                  public rpc_sub_i<rpc::get_slot_leaders>
  {
  public:

    tpu_pub();
    void set_rpc_client( rpc_client * );
    rpc_client *get_rpc_client() const;
    bool init() override;
    void submit( tpu_request * ) override;

  protected:

    void next_slot( uint64_t slot );

    bool         has_curr_;    // have address for current leader
    bool         has_next_;    // have address for next leader
    rpc_client  *clnt_;
    manager     *mgr_;         // facade
    uint64_t     slot_;        // current slot
    ip_addr      curr_ldr_[1]; // current leader ip address
    ip_addr      next_ldr_[1]; // next leader ip address
    udp_socket   tconn_;       // tpu udp connection
    rpc::get_cluster_nodes creq_[1];
    rpc::get_slot_leaders  lreq_[1];
  };

  // embed tpu publisher directly in app
  class tpu_embed : public tpu_pub
  {
  public:

    tpu_embed();
    void set_manager ( manager * );
    manager *get_manager() const;
    void poll() override;

  protected:
    manager *mgr_;
  };

  // publish transaction to remote proxy
  class tpu_proxy : public tpu
  {
  public:

    bool init() override;
    void poll() override;
    void submit( tpu_request * ) override;

  private:
    tcp_connect tconn_;  // tcp connection to proxy server
  };

}
