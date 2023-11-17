#pragma once

#include "client_data.hpp"
#include "util/span.hpp"

#include <array>

struct meta_gem_cond_t
{
  unsigned gem_id;
  unsigned enchant_id;
  std::array<unsigned, 5> lt_operandtype;
  std::array<unsigned, 5> lt_operand;
  std::array<unsigned, 5> op;
  std::array<unsigned, 5> rt_operandtype;
  std::array<unsigned, 5> rt_operand;
  std::array<unsigned, 5> logic;

  static const meta_gem_cond_t& find( unsigned gem_id, bool ptr )
  {
    return dbc::find<meta_gem_cond_t>( gem_id, ptr, &meta_gem_cond_t::gem_id );
  }

  static const meta_gem_cond_t& nil()
  {
    return dbc::nil<meta_gem_cond_t>;
  }

  static util::span<const meta_gem_cond_t> data( bool ptr );
};
