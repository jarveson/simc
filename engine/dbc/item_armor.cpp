#include <array>

#include "config.hpp"

#include "item_armor.hpp"

#include "generated/item_armor.inc"
#if SC_USE_PTR == 1
#include "generated/item_armor_ptr.inc"
#endif

// todo: move / remove me, taken from 4.3.4 client, missing from beta client
static const std::array<item_armor_location_data_t, 24> __const_armor_slot_data = {
    { { 1, { 0.130000, 0.130000, 0.130000, 0.130000 } },  { 2, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 3, { 0.120000, 0.120000, 0.120000, 0.120000 } },  { 4, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 5, { 0.160000, 0.160000, 0.160000, 0.160000 } },  { 6, { 0.090000, 0.090000, 0.090000, 0.090000 } },
      { 7, { 0.140000, 0.140000, 0.140000, 0.140000 } },  { 8, { 0.110000, 0.110000, 0.110000, 0.110000 } },
      { 9, { 0.070000, 0.070000, 0.070000, 0.070000 } },  { 10, { 0.100000, 0.100000, 0.100000, 0.100000 } },
      { 11, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 12, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 13, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 14, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 15, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 16, { 0.080000, 0.080000, 0.080000, 0.080000 } },
      { 17, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 18, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 19, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 20, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 21, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 22, { 0.000000, 0.000000, 0.000000, 0.000000 } },
      { 23, { 0.000000, 0.000000, 0.000000, 0.000000 } }, { 0, { 0, 0, 0, 0 } } } };

util::span<const item_armor_quality_data_t> item_armor_quality_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __item_armor_quality_data, __ptr_item_armor_quality_data, ptr );
}

util::span<const item_armor_shield_data_t> item_armor_shield_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __item_armor_shield_data, __ptr_item_armor_shield_data, ptr );
}

util::span<const item_armor_total_data_t> item_armor_total_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __item_armor_total_data, __ptr_item_armor_total_data, ptr );
}

util::span<const item_armor_location_data_t> item_armor_location_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __const_armor_slot_data, __ptr_armor_location_data, ptr );
}

