#include "item_reforge_data.hpp"

#include "generated/reforge_data.inc"

const item_reforge_data_t& item_reforge_data_t::find( unsigned id, bool ptr )
{
  const auto __data = data( ptr );
  auto it           = range::lower_bound( __data, id, {}, &item_reforge_data_t::id );
  if ( it != __data.end() && it->id == id )
    return *it;
  return item_reforge_data_t::nil();
}

util::span<const item_reforge_data_t> item_reforge_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __item_reforge_data, __ptr_item_reforge_data, ptr );
}
