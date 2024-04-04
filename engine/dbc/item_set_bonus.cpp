#include <array>

#include "config.hpp"

#include "item_set_bonus.hpp"

#include "generated/item_set_bonus.inc"
#if SC_USE_PTR == 1
#include "generated/item_set_bonus_ptr.inc"
#endif

util::span<const item_set_bonus_t> item_set_bonus_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __set_bonus_data, __ptr_set_bonus_data, ptr );
}

util::span<item_set_bonus_t> item_set_bonus_t::_data( bool ptr )
{
  return SC_DBC_GET_DATA( __set_bonus_data, __ptr_set_bonus_data, ptr );
}

void item_set_bonus_t::override_set_specs( util::span<const std::tuple<unsigned, specialization_e>> data )
{
  for ( const auto& [set_id, spec] : data )
  {
    for ( auto& set : _data( false ) )
    {
      if ( set.set_id == set_id )
      {
        set.spec = spec;
      }
    }
  }
}
