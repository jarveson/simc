#include "player/player.hpp"
#include "player/glyph.hpp"

// Invalid, trait does not exist in db2
player_glyph_t::player_glyph_t()
  : m_player( nullptr ), m_glyph( &( glyph_property_data_t::nil() ) ), m_spell( spell_data_t::not_found() ), m_enabled( false )
{
}

// Not found, trait exists but is not found amongst player's selected traits
player_glyph_t::player_glyph_t( const player_t* player )
  : m_player( player ),
    m_glyph( &( glyph_property_data_t::nil() ) ),
    m_spell( spell_data_t::not_found() ),
    m_enabled( false )
{
}

// Found and valid
player_glyph_t::player_glyph_t( const player_t* player, const glyph_property_data_t* glyph, bool enabled )
  : m_player( player ), m_glyph( glyph ), m_spell( m_player->find_spell( glyph->spell_id ) ), m_enabled( enabled )
{
}
