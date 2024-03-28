#pragma once

#include "util/span.hpp"

#include "client_data.hpp"

struct random_suffix_data_t
{
  unsigned id;
  const char* suffix;
  unsigned enchant_id[ 5 ];
  unsigned enchant_alloc[ 5 ];

  static const random_suffix_data_t& find( unsigned id, bool ptr )
  {
    return dbc::find<random_suffix_data_t>( id, ptr, &random_suffix_data_t::id );
  }

  static const random_suffix_data_t& nil()
  {
    return dbc::nil<random_suffix_data_t>;
  }

  static util::span<const random_suffix_data_t> data( bool ptr );
};

