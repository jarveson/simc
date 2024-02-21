// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "player_talent_points.hpp"
#include "util/util.hpp"
#include "fmt/format.h"

bool player_talent_points_t::validate( const spell_data_t* spell, int tree, int row, int col ) const
{
  return has_row_col( tree, row, col ) || range::any_of( _validity_fns, [spell]( const validity_fn_t& fn ) {
                                      return fn( spell );
                                    } );
}

void player_talent_points_t::clear()
{
  for ( int j = 0; j < MAX_TALENT_TREES; ++j )
  {
    auto& c = _choices[ j ];
    for ( int i = 0; i < c.size(); ++i )
    {
      range::fill( c[i], -1 );   
    }
  }
}

