// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#pragma once

#include "config.hpp"

#include "dbc/data_enums.hh"
#include "sc_enums.hpp"
#include "util/string_view.hpp"

#include <string>
#include <tuple>
#include <vector>

class dbc_t;
struct gem_property_data_t;
struct item_t;
struct item_enchantment_data_t;
struct stat_pair_t;

namespace enchant
{
std::string encoded_enchant_name( const dbc_t&, const item_enchantment_data_t& );

const item_enchantment_data_t& find_item_enchant( const item_t& item, util::string_view name );
std::tuple<const item_enchantment_data_t&, size_t> find_meta_gem( const dbc_t& dbc, util::string_view encoding );
const item_enchantment_data_t& find_meta_gem( const dbc_t& dbc, unsigned gem_id );
meta_gem_e meta_gem_type( const dbc_t& dbc, const item_enchantment_data_t& );
meta_gem_e meta_gem_type( const dbc_t& dbc, unsigned gem_id );
bool passive_enchant( item_t& item, unsigned spell_id );

void initialize_item_enchant( item_t& item, std::vector<stat_pair_t>& stats, special_effect_source_e source,
                              const item_enchantment_data_t& enchant );
item_socket_color initialize_gem( item_t& item, size_t gem_idx );
unsigned initialize_meta_gem( item_t& item, unsigned gem_id, const std::vector<unsigned> gem_colors );
item_socket_color initialize_relic( item_t& item, size_t relic_idx, const gem_property_data_t& gem_property );
}  // namespace enchant
