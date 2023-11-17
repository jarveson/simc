#pragma once

#include "dbc/glyph_data.hpp"
#include "dbc/spell_data.hpp"

struct player_t;

class player_glyph_t
{
  const player_t*     m_player;
  const glyph_property_data_t* m_glyph;
  const spell_data_t* m_spell;
  bool m_enabled;

public:
  player_glyph_t();
  player_glyph_t( const player_t* );
  player_glyph_t( const player_t*, const glyph_property_data_t*, bool enabled );

  bool enabled() const
  { return m_enabled && m_spell->ok(); }

  bool ok() const
  { return enabled(); }

  bool invalid() const
  { return m_player == nullptr; }

  const glyph_property_data_t* glyph()
  { return m_glyph; }

  const spell_data_t* spell() const
  { return m_spell; }

  const spell_data_t* operator->() const
  { return m_spell; }

  operator const spell_data_t*() const
  { return m_spell; }
};
