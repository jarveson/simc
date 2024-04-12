#pragma once

#include "client_data.hpp"
#include "util/span.hpp"
#include "util/string_view.hpp"

struct glyph_property_data_t
{
  const char* name;
  unsigned id;
  unsigned spell_id;
  unsigned slot_flags;

  static const glyph_property_data_t* find( util::string_view name, bool ptr );
  static const glyph_property_data_t& find( unsigned id, bool ptr )
  {
    return dbc::find<glyph_property_data_t>( id, ptr, &glyph_property_data_t::id );
  }

  static const glyph_property_data_t& find_by_spellid( unsigned id, bool ptr );

  static const glyph_property_data_t& nil()
  {
    return dbc::nil<glyph_property_data_t>;
  }

  static util::span<const glyph_property_data_t> data( bool ptr );
};
