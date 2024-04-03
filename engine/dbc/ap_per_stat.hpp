#pragma once

#include "util/span.hpp"

#include "client_data.hpp"

struct ap_per_stat_t
{
  unsigned class_id;
  unsigned ranged_attack_power_per_agility;
  unsigned attack_power_per_agility;
  unsigned attack_power_per_strength;

  static const ap_per_stat_t& find( unsigned class_id )
  {
    return dbc::find<ap_per_stat_t>( class_id, false, &ap_per_stat_t::class_id );
  }

  static util::span<const ap_per_stat_t> data( bool ptr );
};