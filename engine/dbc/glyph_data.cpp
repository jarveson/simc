#include "config.hpp"
#include "util/util.hpp"

#include "glyph_data.hpp"
#include "generated/glyph_data.inc"

#include <array>
#if SC_USE_PTR == 1
#include "generated/glyph_data_ptr.inc"
#endif

util::span<const glyph_property_data_t> glyph_property_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __glyph_property_data, __ptr_glyph_property_data, ptr );
}

const glyph_property_data_t* glyph_property_data_t::find( util::string_view name, bool ptr )
{
  auto _data = data( ptr );
  auto _it   = range::find_if(
      _data, [ name ]( const glyph_property_data_t& entry ) { return util::str_compare_ci( name, entry.name ); } );

  if ( _it != _data.end() )
  {
    return _it;
  }

  return &( nil() );
}
