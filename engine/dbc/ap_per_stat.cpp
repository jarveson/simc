#include <array>

#include "ap_per_stat.hpp"

#include "generated/ap_per_stat.inc"

util::span<const ap_per_stat_t> ap_per_stat_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __ap_per_stat_data, __ptr_ap_per_stat_data, ptr );
}
