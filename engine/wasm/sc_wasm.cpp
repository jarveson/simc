// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "class_modules/class_module.hpp"
#include "dbc/dbc.hpp"
#include "dbc/spell_query/spell_data_expr.hpp"
#include "interfaces/bcp_api.hpp"
#include "interfaces/sc_http.hpp"
#include "lib/fmt/core.h"
#include "player/player.hpp"
#include "player/unique_gear.hpp"
#include "report/reports.hpp"
#include "sim/plot.hpp"
#include "sim/reforge_plot.hpp"
#include "sim/profileset.hpp"
#include "sim/sim.hpp"
#include "sim/scale_factor_control.hpp"
#include "sim/sim_control.hpp"
#include "util/git_info.hpp"
#include "util/io.hpp"

#include <emscripten.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>

extern "C" {

void js_loaded();

void js_send_progress( uint32_t iteration, uint32_t total_iterations, uint32_t phase, uint32_t total_phases,
                       const char *phase_name, const char *subphase_name );
}

namespace wasm_hooks
{

void update_progress( const sim_progress_t &progress, size_t current_phase, size_t total_phases,
                      const std::string &phase_name, const std::string &subphase_name )
{
  using progress_clock                = std::chrono::steady_clock;
  static const auto progress_interval = std::chrono::seconds{ 1 };
  static auto last                    = progress_clock::time_point{ progress_clock::duration::zero() };
  const auto now                      = progress_clock::now();

  if ( ( now - last ) < progress_interval )
    return;

  js_send_progress( progress.current_iterations, progress.total_iterations, current_phase, total_phases,
                    phase_name.c_str(), subphase_name.c_str() );
  last = now;
}

}  // namespace wasm_hooks

namespace
{

void initialize_data()
{
  dbc::init();
  module_t::init();
  unique_gear::register_hotfixes();
  unique_gear::register_special_effects();
  unique_gear::sort_special_effects();
  hotfix::apply();
}

bool cpp_simulate( const std::string &profile )
{
  sim_t simc;
  sim_control_t control;

  control.options.parse_text( profile );
  simc.setup( &control );
  simc.json_file_str   = "/output.json";
  simc.html_file_str   = "/output.html";
  simc.output_file_str = "/output.txt";
  simc.report_progress = 1;

  fmt::print(
      "\nSimulating... ( iterations={}, threads={}, target_error={:.3f}, max_time={:.0f}, "
      "vary_combat_length={:0.2f}, optimal_raid={}, fight_style={} )\n\n",
      simc.iterations, simc.threads, simc.target_error, simc.max_time.total_seconds(), simc.vary_combat_length,
      simc.optimal_raid, simc.fight_style );


  if ( !simc.execute() )
  {
    fmt::print( "Simulation was canceled.\n" );
    return false;
  }

  simc.scaling->analyze();
  simc.plot->analyze();
  simc.reforge_plot->analyze();

  if ( !simc.profilesets->iterate( &simc ) )
  {
    fmt::print( "Simulation profilesets canceled.\n" );
    return false;
  }

  report::print_suite( &simc );
  return true;
}

}  // namespace

// ==========================================================================
// WASM API (must be C compatible)
// ==========================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
int main( int, char ** )
{
  initialize_data();
  js_loaded();
}

EMSCRIPTEN_KEEPALIVE
bool simulate( const char *profile_str )
{
  try
  {
    return cpp_simulate( profile_str );
  }
  catch ( const std::exception &e )
  {
    std::cerr << e.what() << std::endl;
    return NULL;
  }
}
}