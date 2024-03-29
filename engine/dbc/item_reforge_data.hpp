#pragma once

#include "client_data.hpp"
#include "util/util.hpp"

struct item_reforge_data_t
{
  unsigned id;
  unsigned source_stat;
  double source_multiplier;
  unsigned target_stat;

  static const item_reforge_data_t& nil()
  {
    return dbc::nil<item_reforge_data_t>;
  }

  static const item_reforge_data_t& find( unsigned, bool ptr = false );
  static util::span<const item_reforge_data_t> data( bool ptr = false );
  static const item_reforge_data_t& find( unsigned src_stat, unsigned dst_stat, bool ptr = false );
};