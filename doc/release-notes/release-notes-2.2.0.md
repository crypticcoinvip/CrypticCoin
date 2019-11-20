Changelog
=========

IntegralTeam (6):

      dpos: liveness enhancement
      dpos: fix old votes pruning
      add 136000 block checkpoint
      fix z_sendmany help
      dpos: disable excessive logs
      dpos: fix cs_vNodes deadlock

IntegralTeam (7)
      mn: fixed bug with dPoS team calculation
      mn: set rpc method 'mn_resign' visible
      mn: changed service txs validation rules
      mn: new db sync (flushing with coindb synchronously) 
      mn: cached dPoS teams, their save/load (synced with coindb)
      mn: changed DB prune logic 
      mn: deny autovotes until fork, deny dismiss/recall votes in miner until fork