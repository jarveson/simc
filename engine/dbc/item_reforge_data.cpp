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

const item_reforge_data_t& item_reforge_data_t::find( unsigned src_stat, unsigned dst_stat, bool ptr )
{
  const auto __data = data( ptr );
  auto it           = range::find_if( __data, [ & ]( const item_reforge_data_t& item ) {
    return item.source_stat == src_stat && item.target_stat == dst_stat;
  });
  if ( it != __data.end() )
    return *it;
  return item_reforge_data_t::nil();
}

util::span<const item_reforge_data_t> item_reforge_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __item_reforge_data, __ptr_item_reforge_data, ptr );
}
