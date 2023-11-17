// ==========================================================================
// Dedmonwakeen's DPS-DPM Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "config.hpp"

#include "action/parse_buff_effects.hpp"
#include "player/covenant.hpp"
#include "player/pet_spawner.hpp"
#include "report/charts.hpp"
#include "report/highchart.hpp"

#include "simulationcraft.hpp"

namespace
{  // UNNAMED NAMESPACE
// ==========================================================================
// Druid
// ==========================================================================

// Forward declarations
struct druid_t;

namespace pets
{
struct force_of_nature_t;
}

enum form_e : unsigned
{
  CAT_FORM       = 0x1,
  NO_FORM        = 0x2,
  TRAVEL_FORM    = 0x4,
  AQUATIC_FORM   = 0x8,  // Legacy
  BEAR_FORM      = 0x10,
  DIRE_BEAR_FORM = 0x40,  // Legacy
  MOONKIN_FORM   = 0x40000000,
};

enum moon_stage_e
{
  NEW_MOON,
  HALF_MOON,
  FULL_MOON,
  MAX_MOON,
};

enum eclipse_state_e
{
  DISABLED,
  ANY_NEXT,
  IN_SOLAR,
  IN_LUNAR,
  IN_BOTH,
  MAX_STATE
};

struct druid_td_t : public actor_target_data_t
{
  struct dots_t
  {
    dot_t* moonfire;
    dot_t* rake;
    dot_t* rip;
    dot_t* sunfire;
  } dots;

  struct hots_t
  {
    dot_t* frenzied_regeneration;
    dot_t* lifebloom;
    dot_t* regrowth;
    dot_t* rejuvenation;
    dot_t* wild_growth;
  } hots;

  struct buffs_t
  {
  } buff;

  druid_td_t( player_t& target, druid_t& source );

  bool hots_ticking() const;
};

struct snapshot_counter_t
{
  simple_sample_data_t execute;
  simple_sample_data_t tick;
  simple_sample_data_t waste;
  std::vector<buff_t*> buffs;
  const sim_t* sim;
  stats_t* stats;
  bool is_snapped = false;

  snapshot_counter_t( player_t* p, stats_t* s, buff_t* b )
    : execute(), tick(), waste(), sim( p->sim ), stats( s )
  {
    buffs.push_back( b );
  }

  bool check_all()
  {
    double n_up = 0;

    for ( const auto& b : buffs )
      if ( b->check() )
        n_up++;

    if ( n_up == 0 )
    {
      is_snapped = false;
      return false;
    }

    waste.add( n_up - 1 );
    is_snapped = true;
    return true;
  }

  void add_buff( buff_t* b ) { buffs.push_back( b ); }

  void count_execute()
  {
    // Skip iteration 0 for non-debug, non-log sims
    if ( sim->current_iteration == 0 && sim->iterations > sim->threads && !sim->debug && !sim->log )
      return;

    execute.add( check_all() );
  }

  void count_tick()
  {
    // Skip iteration 0 for non-debug, non-log sims
    if ( sim->current_iteration == 0 && sim->iterations > sim->threads && !sim->debug && !sim->log )
      return;

    tick.add( is_snapped );
  }

  void merge( const snapshot_counter_t& other )
  {
    execute.merge( other.execute );
    tick.merge( other.tick );
    waste.merge( other.waste );
  }
};

struct eclipse_handler_t
{
  // final entry in data_array holds # of iterations
  using data_array = std::array<double, eclipse_state_e::MAX_STATE + 1>;
  using iter_array = std::array<unsigned, eclipse_state_e::MAX_STATE>;

  struct
  {
    std::vector<data_array> arrays;
    data_array* wrath;
    data_array* starfire;
    data_array* starsurge;
    data_array* starfall;
    data_array* fury_of_elune;
    data_array* new_moon;
    data_array* half_moon;
    data_array* full_moon;
  } data;

  struct
  {
    std::vector<iter_array> arrays;
    iter_array* wrath;
    iter_array* starfire;
    iter_array* starsurge;
    iter_array* starfall;
    iter_array* fury_of_elune;
    iter_array* new_moon;
    iter_array* half_moon;
    iter_array* full_moon;
  } iter;

  druid_t* p;
  unsigned wrath_counter = 2;
  unsigned wrath_counter_base = 2;
  unsigned starfire_counter = 2;
  unsigned starfire_counter_base = 2;
  eclipse_state_e state = eclipse_state_e::DISABLED;

  eclipse_handler_t( druid_t* player ) : data(), iter(), p( player ) {}

  bool enabled() { return state != eclipse_state_e::DISABLED; }
  void init();

  void cast_wrath();
  void cast_starfire();
  void cast_starsurge();
  void cast_moon( moon_stage_e );
  void tick_starfall();
  void tick_fury_of_elune();

  void advance_eclipse();

  void trigger_both( timespan_t );
  void extend_both( timespan_t );
  void expire_both();

  void reset_stacks();
  void reset_state();

  void datacollection_begin();
  void datacollection_end();
  void merge( const eclipse_handler_t& );
};

template <typename Data, typename Base = action_state_t>
struct druid_action_state_t : public Base, public Data
{
  static_assert( std::is_base_of_v<action_state_t, Base> );
  static_assert( std::is_default_constructible_v<Data> );  // required for initialize
  static_assert( std::is_copy_assignable_v<Data> );  // required for copy_state

  using Base::Base;

  void initialize() override
  {
    Base::initialize();
    *static_cast<Data*>( this ) = Data{};
  }

  std::ostringstream& debug_str( std::ostringstream& s ) override
  {
    Base::debug_str( s );
    if constexpr ( fmt::is_formattable<Data>::value )
      fmt::print( s, " {}", *static_cast<const Data*>( this ) );
    return s;
  }

  void copy_state( const action_state_t* o ) override
  {
    Base::copy_state( o );
    *static_cast<Data*>( this ) = *static_cast<const Data*>( static_cast<const druid_action_state_t*>( o ) );
  }
};

// Static helper functions
template <typename V>
static const spell_data_t* resolve_spell_data( V data )
{
  if constexpr( std::is_invocable_v<decltype( &spell_data_t::ok ), V> )
    return data;
  else if constexpr( std::is_invocable_v<decltype( &buff_t::data ), V> )
    return &data->data();
  else if constexpr( std::is_invocable_v<decltype( &action_t::data ), V> )
    return &data->data();

  assert( false && "Could not resolve find_effect argument to spell data." );
  return nullptr;
}

// finds a spell effect
// 1) first argument can be either player_talent_t, spell_data_t*, buff_t*, action_t*
// 2) if the second argument is player_talent_t, spell_data_t*, buff_t*, or action_t* then only effects that affect it are returned
// 3) if the third (or second if the above does not apply) argument is effect subtype, then the type is assumed to be E_APPLY_AURA
// further arguments can be given to filter for full type + subtype + property
template <typename T, typename U, typename... Ts>
static const spelleffect_data_t& find_effect( T val, U type, Ts&&... args )
{
  const spell_data_t* data = resolve_spell_data<T>( val );

  if constexpr( std::is_same_v<U, effect_subtype_t> )
    return spell_data_t::find_spelleffect( *data, E_APPLY_AURA, type, std::forward<Ts>( args )... );
  else if constexpr( std::is_same_v<U, effect_type_t> )
    return spell_data_t::find_spelleffect( *data, type, std::forward<Ts>( args )... );
  else
  {
    const spell_data_t* affected = resolve_spell_data<U>( type );
 
    if constexpr( std::is_same_v<std::tuple_element_t<0, std::tuple<Ts...>>, effect_subtype_t> )
      return spell_data_t::find_spelleffect( *data, *affected, E_APPLY_AURA, std::forward<Ts>( args )... );
    else if constexpr( std::is_same_v<std::tuple_element_t<0, std::tuple<Ts...>>, effect_type_t> )
      return spell_data_t::find_spelleffect( *data, *affected, std::forward<Ts>( args )... );
   else
     return spell_data_t::find_spelleffect( *data, *affected, E_APPLY_AURA );
  }

  assert( false && "Could not resolve find_effect argument to type/subtype." );
  return spelleffect_data_t::nil();
}

template <typename T, typename U, typename... Ts>
static size_t find_effect_index( T val, U type, Ts&&... args )
{
  return find_effect( val, type, std::forward<Ts>( args )... ).index() + 1;
}

// finds the first effect with a trigger spell
// argument can be either player_talent_t, spell_data_t*, buff_t*, action_t*
template <typename T>
static const spelleffect_data_t& find_trigger( T val )
{
  const spell_data_t* data = resolve_spell_data<T>( val );

  for ( const auto& eff : data->effects() )
  {
    switch ( eff.type() )
    {
      case E_TRIGGER_SPELL:
      case E_TRIGGER_SPELL_WITH_VALUE:
        return eff;
      case E_APPLY_AURA:
      case E_APPLY_AREA_AURA_PARTY:
        switch( eff.subtype() )
        {
          case A_PROC_TRIGGER_SPELL:
          case A_PROC_TRIGGER_SPELL_WITH_VALUE:
          case A_PERIODIC_TRIGGER_SPELL:
          case A_PERIODIC_TRIGGER_SPELL_WITH_VALUE:
            return eff;
          default:
            break;
        }
        break;
      default:
        break;
    }
  }

  return spelleffect_data_t::nil();
}

struct druid_t : public player_t
{
private:
  form_e form = form_e::NO_FORM;  // Active druid form
public:
  std::vector<std::unique_ptr<snapshot_counter_t>> counters;  // counters for snapshot tracking

  // !!!==========================================================================!!!
  // !!! Runtime variables NOTE: these MUST be properly reset in druid_t::reset() !!!
  // !!!==========================================================================!!!
  struct dot_list_t
  {
    std::vector<dot_t*> moonfire;
    std::vector<dot_t*> sunfire;
  } dot_list;
  // !!!==========================================================================!!!

  // Options
  struct options_t
  {
    // General
    bool no_cds = false;
    bool raid_combat = true;

    // Balance
    int initial_moon_stage = static_cast<int>( moon_stage_e::NEW_MOON );

    // Restoration
    double time_spend_healing = 0.0;
  } options;

  struct active_actions_t
  {
    // General
    action_t* shift_to_caster;
    action_t* shift_to_bear;
    action_t* shift_to_cat;
    action_t* shift_to_moonkin;

  } active;

  // Pets
  struct pets_t
  {
    spawner::pet_spawner_t<pets::force_of_nature_t, druid_t> force_of_nature;

    pets_t( druid_t* p )
        : force_of_nature( "force_of_nature", p )
    {}
  } pets;

  // Auto-attacks
  weapon_t caster_form_weapon;
  weapon_t cat_weapon;
  weapon_t bear_weapon;
  melee_attack_t* caster_melee_attack = nullptr;
  melee_attack_t* cat_melee_attack = nullptr;
  melee_attack_t* bear_melee_attack = nullptr;

  // Buffs
  struct buffs_t
  {
    // General
    buff_t* barkskin;
    buff_t* bear_form;
    buff_t* cat_form;
    buff_t* prowl;

    // Class
    buff_t* dash;
    buff_t* innervate;
    buff_t* moonkin_form;
 
    // Multi-Spec
    buff_t* survival_instincts;
    buff_t* clearcasting;

    // Balance
    buff_t* eclipse_lunar;
    buff_t* eclipse_solar;
    buff_t* natures_grace;
    buff_t* owlkin_frenzy;

    // Feral
    buff_t* berserk;
    buff_t* tigers_fury;
    buff_t* predatory_swiftness;
    buff_t* fury_swipes;
    buff_t* stampede_cat;

    // This just functions as a pseudo counter
    // for bloodletting glyph
    unsigned rip_extensions;

    // Guardian
    buff_t* blood_frenzy;
    buff_t* stampede_bear;
    buff_t* pulverize;
    buff_t* enrage;

    // Restoration
    buff_t* natures_swiftness;
    buff_t* harmony;
    buff_t* tree_of_life;
  } buff;

  // Cooldowns
  struct cooldowns_t
  {
    cooldown_t* berserk;
    cooldown_t* frenzied_regeneration;
    cooldown_t* mangle;
    cooldown_t* moon_cd;  // New / Half / Full Moon
    cooldown_t* natures_swiftness;
    cooldown_t* tigers_fury;
    cooldown_t* tree_of_life;
  } cooldown;

  // Gains
  struct gains_t
  {
    // Multiple Specs / Forms
    gain_t* clearcasting;        // Feral & Restoration

    // Balance
    gain_t* natures_balance;
    gain_t* stellar_innervation;

    // Feral (Cat)
    gain_t* energy_refund;
    gain_t* primal_fury;
    gain_t* tigers_tenacity;

    // Guardian (Bear)
    gain_t* bear_form;
    gain_t* rage_from_melees;
    gain_t* primal_madness;
    gain_t* enrage;
  } gain;

  // Masteries
  struct masteries_t
  {
    const spell_data_t* harmony;
    const spell_data_t* savage_defender;
    const spell_data_t* razor_claws;
    const spell_data_t* total_eclipse;
  } mastery;

  struct glyphs_t
  {
    player_glyph_t berserk;
    player_glyph_t bloodletting;
    player_glyph_t ferocious_bite;
    player_glyph_t focus;
    player_glyph_t frenzied_regeneration;
    player_glyph_t healing_touch;
    player_glyph_t innervate;
    player_glyph_t insect_swarm;
    player_glyph_t lacerate;
    player_glyph_t lifebloom;
    player_glyph_t mangle;
    player_glyph_t mark_of_the_wild;
    player_glyph_t maul;
    player_glyph_t monsoon;
    player_glyph_t moonfire;
    player_glyph_t regrowth;
    player_glyph_t rejuvenation;
    player_glyph_t rip;
    player_glyph_t savage_roar;
    player_glyph_t starfall;
    player_glyph_t starfire;
    player_glyph_t starsurge;
    player_glyph_t swiftmend;
    player_glyph_t tigers_fury;
    player_glyph_t typhoon;
    player_glyph_t wild_growth;
    player_glyph_t wrath;
  } glyphs;

  // Procs
  struct procs_t
  {
    // Balance

    // Feral
    proc_t* clearcasting;
    proc_t* clearcasting_wasted;
    proc_t* primal_fury;
    proc_t* fury_swipes;

    // Guardian
  } proc;

  // Talents
  struct talents_t
  {
    // Balance
    player_talent_t balance_of_power;
    player_talent_t earth_and_moon;
    player_talent_t euphoria;
    player_talent_t dreamstate;
    player_talent_t force_of_nature;
    player_talent_t fungal_growth;
    player_talent_t gale_winds;
    player_talent_t genesis;
    player_talent_t lunar_shower;
    player_talent_t moonglow;
    player_talent_t moonkin_form;
    player_talent_t natures_grace;
    player_talent_t natures_majesty;
    player_talent_t owlkin_frenzy;
    player_talent_t shooting_stars;
    player_talent_t solar_beam;
    player_talent_t starfall;
    player_talent_t starlight_wrath;
    player_talent_t sunfire;
    player_talent_t typhoon;

    // Feral
    player_talent_t berserk;
    player_talent_t blood_in_the_water;
    player_talent_t brutal_impact;
    player_talent_t endless_carnage;
    player_talent_t feral_aggression;
    player_talent_t feral_charge;
    player_talent_t feral_swiftness;
    player_talent_t furor;
    player_talent_t fury_swipes;
    player_talent_t infected_wounds;
    player_talent_t king_of_the_jungle;
    player_talent_t leader_of_the_pack;
    player_talent_t natural_reaction;
    player_talent_t nurturing_instict;  // NYI
    player_talent_t predatory_strikes;
    player_talent_t primal_fury;
    player_talent_t primal_madness;
    player_talent_t pulverize;
    player_talent_t rend_and_tear;
    player_talent_t stampede;
    player_talent_t survival_instincts;
    player_talent_t thick_hide;

    player_talent_t blessing_of_the_grove;
    player_talent_t efflorescence;
    player_talent_t empowered_touch;
    player_talent_t fury_of_stormrage;  // NYI
    player_talent_t gift_of_the_earthmother;
    player_talent_t heart_of_the_wild;
    player_talent_t improved_rejuvenation;
    player_talent_t living_seed;
    player_talent_t malfurions_gift;
    player_talent_t master_shapeshifter;
    player_talent_t natural_shapeshifter;
    player_talent_t natures_bounty;
    player_talent_t natures_cure;  // NYI
    player_talent_t natures_swiftness;
    player_talent_t natures_ward;  // NYI
    player_talent_t naturalist;
    player_talent_t perseverance;
    player_talent_t revitalize;
    player_talent_t swift_rejuvenation;
    player_talent_t tree_of_life;
    player_talent_t wild_growth;

  } talent;

  // Class Specializations
  struct specializations_t
  {
    const spell_data_t* leather_specialization;

    const spell_data_t* starsurge;
    const spell_data_t* moonfury;

    const spell_data_t* aggression;
    const spell_data_t* vengeance;
    const spell_data_t* mangle;
    //const spell_data_t* feral_instinct;

    const spell_data_t* swiftmend;
    const spell_data_t* meditation;
    const spell_data_t* gift_of_nature;
    //const spell_data_t* disentanglement;

    // prolly not spec specific
    const spell_data_t* bear_form_passive;
    const spell_data_t* cat_form_passive;

  } spec;

  struct uptimes_t
  {
  } uptime;

  druid_t( sim_t* sim, std::string_view name, race_e r = RACE_NIGHT_ELF )
    : player_t( sim, DRUID, name, r ),
      options(),
      active(),
      pets( this ),
      caster_form_weapon(),
      buff(),
      cooldown(),
      gain(),
      mastery(),
      proc(),
      talent(),
      spec(),
      uptime()
  {
    cooldown.berserk               = get_cooldown( "berserk" );
    cooldown.frenzied_regeneration = get_cooldown( "frenzied_regeneration" );
    cooldown.mangle                = get_cooldown( "mangle" );
    cooldown.moon_cd               = get_cooldown( "moon_cd" );
    cooldown.natures_swiftness     = get_cooldown( "natures_swiftness" );
    cooldown.tigers_fury           = get_cooldown( "tigers_fury" );
    cooldown.tree_of_life          = get_cooldown("tree_of_life");

    resource_regeneration = regen_type::DYNAMIC;
    buff.rip_extensions        = 0;

    regen_caches[ CACHE_HASTE ]        = true;
    regen_caches[ CACHE_ATTACK_HASTE ] = true;
  }

  // Character Definition
  void activate() override;
  void init_absorb_priority() override;
  void init_action_list() override;
  void init_base_stats() override;
  void init_gains() override;
  void init_procs() override;
  void init_uptimes() override;
  void init_resources( bool ) override;
  void init_rng() override;
  void init_special_effects() override;
  void init_spells() override;
  void init_scaling() override;
  void init_finished() override;
  void create_buffs() override;
  void create_actions() override;
  std::string default_flask() const override;
  std::string default_potion() const override;
  std::string default_food() const override;
  std::string default_rune() const override;
  std::string default_temporary_enchant() const override;
  void invalidate_cache( cache_e ) override;
  void reset() override;
  void merge( player_t& other ) override;
  void datacollection_begin() override;
  void datacollection_end() override;
  timespan_t available() const override;
  double composite_attack_power_multiplier() const override;
  double composite_armor_multiplier() const override;
  double composite_melee_crit_chance() const override;
  double composite_spell_crit_chance() const override;
  double composite_block() const override { return 0; }
  double composite_crit_avoidance() const override;
  double composite_dodge_rating() const override;
  double composite_parry() const override { return 0; }
  double composite_attribute_multiplier( attribute_e attr ) const override;
  double matching_gear_multiplier( attribute_e attr ) const override;
  double composite_melee_expertise( const weapon_t* ) const override;
  double temporary_movement_modifier() const override;
  double passive_movement_modifier() const override;
  std::unique_ptr<expr_t> create_action_expression(action_t& a, std::string_view name_str) override;
  action_t* create_action( std::string_view name, std::string_view options ) override;
  resource_e primary_resource() const override;
  role_e primary_role() const override;
  stat_e convert_hybrid_stat( stat_e s ) const override;
  double resource_regen_per_second( resource_e ) const override;
  void target_mitigation( school_e, result_amount_type, action_state_t* ) override;
  void assess_damage_imminent_pre_absorb( school_e, result_amount_type, action_state_t* ) override;
  void create_options() override;
  const druid_td_t* find_target_data( const player_t* target ) const override;
  druid_td_t* get_target_data( player_t* target ) const override;
  void copy_from( player_t* ) override;
  form_e get_form() const { return form; }
  void shapeshift( form_e );
  void init_beast_weapon( weapon_t&, double );
  void moving() override;
  const spell_data_t* apply_override( const spell_data_t* base, const spell_data_t* passive );
  void apply_affecting_auras( action_t& ) override;

  // secondary actions
  std::vector<action_t*> secondary_action_list;

  template <typename T, typename... Ts>
  T* get_secondary_action( std::string_view n, Ts&&... args );

private:
  void apl_precombat();
  void apl_default();
  void apl_feral();
  void apl_balance();
  void apl_balance_ptr();
  void apl_guardian();
  void apl_restoration();

  target_specific_t<druid_td_t> target_data;
};

namespace pets
{
// ==========================================================================
// Pets and Guardians
// ==========================================================================

// Force of Nature ==================================================
struct force_of_nature_t : public pet_t
{
  struct fon_melee_t : public melee_attack_t
  {
    bool first_attack = true;

    fon_melee_t( pet_t* pet, const char* name = "Melee" ) : melee_attack_t( name, pet, spell_data_t::nil() )
    {
      school            = SCHOOL_PHYSICAL;
      weapon            = &( pet->main_hand_weapon );
      weapon_multiplier = 1.0;
      base_execute_time = weapon->swing_time;
      may_crit = background = repeating = true;
    }

    timespan_t execute_time() const override
    {
      return first_attack ? 0_ms : melee_attack_t::execute_time();
    }

    void cancel() override
    {
      melee_attack_t::cancel();
      first_attack = true;
    }

    void schedule_execute( action_state_t* s ) override
    {
      melee_attack_t::schedule_execute( s );
      first_attack = false;
    }
  };

  struct auto_attack_t : public melee_attack_t
  {
    auto_attack_t( pet_t* pet ) : melee_attack_t( "auto_attack", pet )
    {
      assert( pet->main_hand_weapon.type != WEAPON_NONE );
      pet->main_hand_attack = new fon_melee_t( pet );
      trigger_gcd = 0_ms;
    }

    void execute() override { player->main_hand_attack->schedule_execute(); }

    bool ready() override { return ( player->main_hand_attack->execute_event == nullptr ); }
  };

  druid_t* o() { return static_cast<druid_t*>( owner ); }

  force_of_nature_t( druid_t* p ) : pet_t( p->sim, p, "Treant", true, true )
  {
    // Treants have base weapon damage + ap from player's sp.
    owner_coeff.ap_from_sp = 0.6;

    double base_dps = o()->dbc->expected_stat( o()->true_level ).creature_auto_attack_dps;

    main_hand_weapon.min_dmg = main_hand_weapon.max_dmg = base_dps * main_hand_weapon.swing_time.total_seconds() / 1000;

    resource_regeneration = regen_type::DISABLED;
    main_hand_weapon.type = WEAPON_BEAST;

    action_list_str = "auto_attack";
  }

  void init_base_stats() override
  {
    pet_t::init_base_stats();

    // TODO: confirm these values
    resources.base[ RESOURCE_HEALTH ] = owner->resources.max[ RESOURCE_HEALTH ] * 0.4;
    resources.base[ RESOURCE_MANA ]   = 0;

    initial.stats.attribute[ ATTR_INTELLECT ] = 0;
    initial.spell_power_per_intellect         = 0;
    intellect_per_owner                       = 0;
    stamina_per_owner                         = 0;
  }

  resource_e primary_resource() const override { return RESOURCE_NONE; }

  action_t* create_action( std::string_view name, std::string_view opt ) override
  {
    if ( name == "auto_attack" ) return new auto_attack_t( this );

    return pet_t::create_action( name, opt );
  }
};

std::function<void( pet_t* )> parent_pet_action_fn( action_t* parent )
{
  return [ parent ]( pet_t* p ) {
    for ( auto a : p->action_list )
    {
      auto it = range::find( parent->child_action, a->name_str, &action_t::name_str );
      if ( it != parent->child_action.end() )
        a->stats = ( *it )->stats;
      else
        parent->add_child( a );
    }
  };
}
}  // end namespace pets

namespace buffs
{
template <typename Base = buff_t>
struct druid_buff_base_t : public Base
{
  static_assert( std::is_base_of_v<buff_t, Base> );

protected:
  using base_t = druid_buff_base_t<Base>;

public:
  druid_buff_base_t( druid_t* p, std::string_view name, const spell_data_t* s = spell_data_t::nil(),
                     const item_t* item = nullptr )
    : Base( p, name, s, item )
  {}

  druid_buff_base_t( druid_td_t& td, std::string_view name, const spell_data_t* s = spell_data_t::nil(),
                     const item_t* item = nullptr )
    : Base( td, name, s, item )
  {}

  druid_t* p() { return static_cast<druid_t*>( Base::source ); }

  const druid_t* p() const { return static_cast<druid_t*>( Base::source ); }
};

using druid_buff_t = druid_buff_base_t<>;

// Shapeshift Form Buffs ====================================================

struct swap_melee_t
{
private:
  druid_t* p_;

public:
  swap_melee_t( druid_t* p ) : p_( p ) {}

  // Used when shapeshifting to switch to a new attack & schedule it to occur
  // when the current swing timer would have ended.
  void swap_melee( attack_t* new_attack, weapon_t& new_weapon )
  {
    if ( p_->main_hand_attack && p_->main_hand_attack->execute_event )
    {
      new_attack->base_execute_time = new_weapon.swing_time;
      new_attack->execute_event =
          new_attack->start_action_execute_event( p_->main_hand_attack->execute_event->remains() );
      p_->main_hand_attack->cancel();
    }
    new_attack->weapon = &new_weapon;
    p_->main_hand_attack = new_attack;
    p_->main_hand_weapon = new_weapon;
  }
};

// Bear Form ================================================================
struct bear_form_buff_t : public druid_buff_t, public swap_melee_t
{
  double rage_gain;

  bear_form_buff_t( druid_t* p )
    : base_t( p, "bear_form", p->find_class_spell( "Bear Form" ) ),
      swap_melee_t( p ),
      rage_gain( find_effect( p->find_spell( 17057 ), E_ENERGIZE ).resource( RESOURCE_RAGE ) )
  {
    add_invalidate( CACHE_ARMOR );
    add_invalidate( CACHE_ATTACK_POWER );
    add_invalidate( CACHE_CRIT_AVOIDANCE );
    add_invalidate( CACHE_EXP );
    add_invalidate( CACHE_HIT );
    add_invalidate( CACHE_PLAYER_DAMAGE_MULTIPLIER );
    add_invalidate( CACHE_STAMINA );
  }

  void expire_override( int expiration_stacks, timespan_t remaining_duration ) override
  {
    base_t::expire_override( expiration_stacks, remaining_duration );

    swap_melee( p()->caster_melee_attack, p()->caster_form_weapon );

    p()->resource_loss( RESOURCE_RAGE, p()->resources.current[ RESOURCE_RAGE ] );
    p()->recalculate_resource_max( RESOURCE_HEALTH );
  }

  void start( int stacks, double value, timespan_t duration ) override
  {
    swap_melee( p()->bear_melee_attack, p()->bear_weapon );

    base_t::start( stacks, value, duration );

    p()->resource_gain( RESOURCE_RAGE, rage_gain, p()->gain.bear_form );
    p()->recalculate_resource_max( RESOURCE_HEALTH );
  }
};

// Cat Form =================================================================
struct cat_form_buff_t : public druid_buff_t, public swap_melee_t
{
  cat_form_buff_t( druid_t* p ) : base_t( p, "cat_form", p->find_class_spell( "Cat Form" ) ), swap_melee_t( p )
  {
    add_invalidate( CACHE_ATTACK_POWER );
    add_invalidate( CACHE_EXP );
    add_invalidate( CACHE_HIT );
  }

  void expire_override( int expiration_stacks, timespan_t remaining_duration ) override
  {
    base_t::expire_override( expiration_stacks, remaining_duration );

    swap_melee( p()->caster_melee_attack, p()->caster_form_weapon );
  }

  void start( int stacks, double value, timespan_t duration ) override
  {
    swap_melee( p()->cat_melee_attack, p()->cat_weapon );

    base_t::start( stacks, value, duration );
  }
};

// Moonkin Form =============================================================
struct moonkin_form_buff_t : public druid_buff_t
{
  moonkin_form_buff_t( druid_t* p ) : base_t( p, "moonkin_form", p->talent.moonkin_form )
  {
    add_invalidate( CACHE_ARMOR );
    add_invalidate( CACHE_EXP );
    add_invalidate( CACHE_HIT );
  }
};
}  // end namespace buffs

// Template for common druid action code. See priest_action_t.
template <class Base>
struct druid_action_t : public Base, public parse_buff_effects_t<druid_td_t>
{
private:
  using ab = Base;  // action base, eg. spell_t

public:
  using base_t = druid_action_t<Base>;

  // Name to be used by get_dot() instead of action name
  std::string dot_name;
  // form spell to automatically cast
  action_t* autoshift = nullptr;
  // Restricts use of a spell based on form.
  unsigned form_mask;
  // Allows a spell that may be cast in NO_FORM but not in current form to be cast by exiting form.
  bool may_autounshift = true;
  bool is_auto_attack = false;
  bool break_stealth;
  position_e requires_position = POSITION_NONE;
  bool ignore_stealth          = false;

  druid_action_t( std::string_view n, druid_t* player, const spell_data_t* s = spell_data_t::nil() )
    : ab( n, player, s ),
      parse_buff_effects_t( this ),
      dot_name( n ),
      form_mask( ab::data().stance_mask() ),
      break_stealth( !ab::data().flags( spell_attribute::SX_NO_STEALTH_BREAK ) ),
      requires_position( POSITION_NONE )
  {
    // WARNING: auto attacks will NOT get processed here since they have no spell data
    if ( ab::data().ok() )
    {
      apply_buff_effects();

      if ( ab::type == action_e::ACTION_SPELL || ab::type == action_e::ACTION_ATTACK )
        apply_debuffs_effects();

      if ( ab::data().flags( spell_attribute::SX_ABILITY ) || ab::trigger_gcd > 0_ms )
        ab::not_a_proc = true;
    }
  }

  druid_t* p() { return static_cast<druid_t*>( ab::player ); }

  const druid_t* p() const { return static_cast<druid_t*>( ab::player ); }

  druid_td_t* td( player_t* t ) const { return p()->get_target_data( t ); }

  static std::string get_suffix( std::string_view name, std::string_view base )
  {
    return std::string( name.substr( std::min( name.size(), name.find( base ) + base.size() ) ) );
  }

  dot_t* get_dot( player_t* t = nullptr ) override
  {
    if ( !t )
      t = ab::target;
    if ( !t )
      return nullptr;

    dot_t*& dot = ab::target_specific_dot[ t ];
    if ( !dot )
      dot = t->get_dot( dot_name, ab::player );
    return dot;
  }

  bool ready() override
  {
    // stealth class
    if ( !ignore_stealth && (form_mask & 0x20000000 ))
    {
      if ( !p()->buff.prowl->check() && !p()->buffs.shadowmeld->check() )
      {
        if ( ab::sim->debug )
        {
          ab::sim->print_debug( "{} ready() failed due to no stealth. form={:#010x} form_mask={:#010x}", ab::name(),
                                static_cast<unsigned int>( p()->get_form() ), form_mask );
        }
        return false;
      }
    }

    if ( !check_form_restriction() && !( ( may_autounshift && ( form_mask & NO_FORM ) == NO_FORM ) || autoshift ) )
    {
      if ( ab::sim->debug )
      {
        ab::sim->print_debug( "{} ready() failed due to wrong form. form={:#010x} form_mask={:#010x}", ab::name(),
                              static_cast<unsigned int>( p()->get_form() ), form_mask );
      }

      return false;
    }

    if ( requires_position != POSITION_NONE && requires_position != p()->position() )
      return false;

    return ab::ready();
  }

  void snapshot_and_execute( const action_state_t* s, bool is_dot,
                             std::function<void( const action_state_t*, action_state_t* )> pre = nullptr,
                             std::function<void( const action_state_t*, action_state_t* )> post = nullptr )
  {
    auto state = this->get_state();
    this->target = state->target = s->target;

    if ( pre )
      pre( s, state );

    this->snapshot_state( state, this->amount_type( state, is_dot ) );

    if ( post )
      post( s, state );

    this->schedule_execute( state );
  }

  void init() override;
  void schedule_execute( action_state_t* s = nullptr ) override;
  void execute() override;

  void apply_buff_effects()
  {
    // Class
    parse_buff_effects( p()->buff.clearcasting );
    parse_buff_effects( p()->buff.cat_form );
    parse_buff_effects( p()->buff.moonkin_form );

    //parse_buff_effects( p()->spec.gift_of_nature );

    // Balance
    parse_buff_effects( p()->buff.eclipse_lunar, 0b0111110U, true, USE_CURRENT );
    parse_buff_effects( p()->buff.eclipse_solar, 0b01111110U, true, USE_CURRENT );
    //parse_buff_effects( p()->buff.incarnation_moonkin, p()->talent.elunes_guidance );
    parse_buff_effects( p()->buff.owlkin_frenzy );

    // Feral
    parse_buff_effects( p()->buff.berserk );
    parse_buff_effects( p()->buff.predatory_swiftness );
    parse_buff_effects( p()->buff.stampede_cat );

    // Guardian
    parse_buff_effects( p()->buff.bear_form );
    parse_buff_effects( p()->buff.stampede_bear );
    parse_buff_effects( p()->buff.enrage );
    //parse_buff_effects( p()->buff.furious_regeneration );

    // Restoration
    parse_buff_effects( p()->buff.tree_of_life );
    parse_buff_effects( p()->buff.natures_swiftness);
    parse_buff_effects( p()->buff.harmony );
  }

  void apply_debuffs_effects()
  {
    //parse_dot_effects( &druid_td_t::dots_t::moonfire, p()->spec.moonfire_dmg);
    //parse_dot_effects( &druid_td_t::dots_t::sunfire, p()->spec.sunfire_dmg);
  }

  template <typename DOT, typename... Ts>
  void parse_dot_effects( DOT dot, const spell_data_t* spell, bool stacks, Ts... mods )
  {
    if ( stacks )
    {
      parse_debuff_effects( [ dot ]( druid_td_t* t ) {
        return std::invoke( dot, t->dots )->current_stack();
      }, spell, mods... );
    }
    else
    {
      parse_debuff_effects( [ dot ]( druid_td_t* t ) {
        return std::invoke( dot, t->dots )->is_ticking();
      }, spell, mods... );
    }
  }

  template <typename DOT, typename... Ts>
  void parse_dot_effects( DOT dot, const spell_data_t* spell, Ts... mods )
  {
    parse_dot_effects( dot, spell, true, mods... );
  }

  template <typename DOT, typename... Ts>
  void force_dot_effect( DOT dot, const spell_data_t* spell, unsigned idx, Ts... mods )
  {
    if ( ab::data().affected_by_all( spell->effectN( idx ) ) )
      return;

    parse_debuff_effect( [ dot ]( druid_td_t* t ) {
      return std::invoke( dot, t->dots )->is_ticking();
    }, spell, idx, true, mods... );
  }

  double cost() const override
  {
    double c = ab::cost();

    c += get_buff_effects_value( flat_cost_buffeffects, true, false );

    c *= get_buff_effects_value( cost_buffeffects, false, false );

    return std::max( 0.0, c );
  }

  double composite_ta_multiplier( const action_state_t* s ) const override
  {
    return ab::composite_ta_multiplier( s ) * get_buff_effects_value( ta_multiplier_buffeffects );
  }

  double composite_da_multiplier( const action_state_t* s ) const override
  {
    return ab::composite_da_multiplier( s ) * get_buff_effects_value( da_multiplier_buffeffects );
  }

  double composite_crit_chance() const override
  {
    return ab::composite_crit_chance() + get_buff_effects_value( crit_chance_buffeffects, true );
  }

  timespan_t execute_time() const override
  {
    return std::max( 0_ms, ab::execute_time() * get_buff_effects_value( execute_time_buffeffects ) );

  }

  timespan_t composite_dot_duration( const action_state_t* s ) const override
  {
    return ab::composite_dot_duration( s ) * get_buff_effects_value( dot_duration_buffeffects );
  }

  timespan_t tick_time( const action_state_t* s ) const override
  {
    return ab::tick_time( s ) * get_buff_effects_value( tick_time_buffeffects );
  }

  timespan_t cooldown_duration() const override
  {
    return ab::cooldown_duration() * get_buff_effects_value( recharge_multiplier_buffeffects );
  }

  double recharge_multiplier( const cooldown_t& cd ) const override
  {
    return ab::recharge_multiplier( cd ) * get_buff_effects_value( recharge_multiplier_buffeffects );
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    return ab::composite_target_multiplier( t ) * get_debuff_effects_value( td( t ) );
  }

  // Override this function for temporary effects that change the normal form restrictions of the spell. eg: Predatory
  // Swiftness
  virtual bool check_form_restriction()
  {
    if ( !form_mask || ( form_mask & p()->get_form() ) == p()->get_form() )
      return true;

    return false;
  }

  void check_autoshift()
  {
    if ( !check_form_restriction() )
    {
      if ( may_autounshift && ( form_mask & NO_FORM ) == NO_FORM )
        p()->active.shift_to_caster->execute();
      else if ( autoshift )
        autoshift->execute();
      else
        assert( false && "Action executed in wrong form with no valid form to shift to!" );
    }
  }

  void html_customsection( report::sc_html_stream& os ) override
  {
    parsed_html_report( os );
  }

  void trigger_mangle( action_state_t* s )
  {
    if ( ab::sim->overrides.mangle )
    {
      return;
    }

    if ( ab::result_is_miss( s->result ) )
    {
      return;
    }

    s->target->debuffs.bleed_dmg_taken->trigger();
  }

  void trigger_bleed( action_state_t* s, timespan_t d )
  {
    if ( ab::sim->overrides.bleeding )
      return;

    if ( ab::result_is_miss( s->result ) )
      return;

    s->target->debuffs.bleeding->trigger(d);
  }
};

template <typename BASE>
struct use_dot_list_t : public BASE
{
private:
  druid_t* p_;

public:
  using base_t = use_dot_list_t<BASE>;

  std::vector<dot_t*>* dot_list = nullptr;

  use_dot_list_t( std::string_view n, druid_t* p, const spell_data_t* s ) : BASE( n, p, s), p_( p ) {}

  void init() override
  {
    assert( dot_list );

    BASE::init();
  }

  void trigger_dot( action_state_t* s ) override
  {
    dot_t* d = BASE::get_dot( s->target );
    if( !d->is_ticking() )
    {
      assert( !range::contains( *dot_list, d ) );
      dot_list->push_back( d );
    }

    BASE::trigger_dot( s );
  }

  void last_tick( dot_t* d ) override
  {
    assert( range::contains( *dot_list, d ) );
    dot_list->erase( std::remove( dot_list->begin(), dot_list->end(), d ), dot_list->end() );

    BASE::last_tick( d );
  }
};

// Druid melee attack base for cat_attack_t and bear_attack_t
template <class Base>
struct druid_attack_t : public druid_action_t<Base>
{
private:
  using ab = druid_action_t<Base>;

public:
  using base_t = druid_attack_t<Base>;

  double bleed_mul = 0.0;
  bool direct_bleed = false;

  druid_attack_t( std::string_view n, druid_t* player, const spell_data_t* s = spell_data_t::nil() )
    : ab( n, player, s )
  {
    ab::may_glance = false;
    ab::special    = true;

    parse_special_effect_data();
  }

  void parse_special_effect_data()
  {
    for ( size_t i = 1; i <= this->data().effect_count(); i++ )
    {
      const spelleffect_data_t& ed = this->data().effectN( i );
      effect_type_t type           = ed.type();

      // Check for bleed flag at effect or spell level.
      if ( ( type == E_SCHOOL_DAMAGE || type == E_WEAPON_PERCENT_DAMAGE ) &&
           ( ed.mechanic() == MECHANIC_BLEED || this->data().mechanic() == MECHANIC_BLEED ) )
      {
        direct_bleed = true;
      }
    }
  }

  void impact( action_state_t* s ) override
  {
    if ( direct_bleed && ab::result_is_hit( s->result ) )
    {
      dot_t* dot = ab::find_dot( s->target );
      assert( dot );
      ab::trigger_bleed( s, dot->duration() );
    }
  }

  double composite_target_armor( player_t* t ) const override
  {
    return direct_bleed ? 0.0 : ab::composite_target_armor( t );
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    double tm = ab::composite_target_multiplier( t );

    if ( bleed_mul && t->debuffs.bleeding->up() )
      tm *= 1.0 + bleed_mul;

    if (direct_bleed && t->debuffs.bleed_dmg_taken->up())
      tm *= 1.0 + t->debuffs.bleed_dmg_taken->check_value();
    return tm;
  }
};

// Druid "Spell" Base for druid_spell_t, druid_heal_t ( and potentially druid_absorb_t )
template <class Base>
struct druid_spell_base_t : public druid_action_t<Base>
{
private:
  using ab = druid_action_t<Base>;

public:
  using base_t = druid_spell_base_t<Base>;

  bool reset_melee_swing = true;  // TRUE(default) to reset swing timer on execute (as most cast time spells do)

  druid_spell_base_t( std::string_view n, druid_t* player, const spell_data_t* s = spell_data_t::nil() )
    : ab( n, player, s )
  {}

  void execute() override
  {
    if ( ab::trigger_gcd > 0_ms && !ab::proc && !ab::background && reset_melee_swing &&
         ab::p()->main_hand_attack && ab::p()->main_hand_attack->execute_event )
    {
      ab::p()->main_hand_attack->execute_event->reschedule( ab::p()->main_hand_weapon.swing_time *
                                                            ab::p()->cache.attack_speed() );
    }

    ab::execute();
  }
};

// for child actions that do damage based on % of parent action
struct druid_residual_data_t
{
  double total_amount = 0.0;

  void sc_format_to( const druid_residual_data_t& data, fmt::format_context::iterator out )
  {
    fmt::format_to( out, "total_amount={}", data.total_amount );
  }
};

template <class Base, class Data = druid_residual_data_t>
struct druid_residual_action_t : public Base
{
  using base_t = druid_residual_action_t<Base, Data>;
  using state_t = druid_action_state_t<Data>;

  double residual_mul = 0.0;

  druid_residual_action_t( std::string_view n, druid_t* p, const spell_data_t* s ) : Base( n, p, s )
  {
    Base::background = true;
    Base::round_base_dmg = false;

    Base::attack_power_mod.direct = Base::attack_power_mod.tick = 0;
    Base::spell_power_mod.direct = Base::spell_power_mod.tick = 0;

    // 1 point to allow proper snapshot/update flag parsing
    Base::base_dd_min = Base::base_dd_max = 1.0;
  }

  action_state_t* new_state() override
  {
    return new state_t( this, Base::target );
  }

  state_t* cast_state( action_state_t* s )
  {
    return static_cast<state_t*>( s );
  }

  const state_t* cast_state( const action_state_t* s ) const
  {
    return static_cast<const state_t*>( s );
  }

  void set_amount( action_state_t* s, double v )
  {
    cast_state( s )->total_amount = v;
  }

  virtual double get_amount( const action_state_t* s ) const
  {
    return cast_state( s )->total_amount * residual_mul;
  }

  double base_da_min( const action_state_t* s ) const override
  {
    return get_amount( s );
  }

  double base_da_max( const action_state_t* s ) const override
  {
    return get_amount( s );
  }

  void init() override
  {
    Base::init();
    Base::update_flags &= ~( STATE_MUL_TA );
  }
};

// constructor macro for foreground abilities
#define DRUID_ABILITY( _class, _base, _name, _spell ) \
  _class( druid_t* p, std::string_view n = _name, const spell_data_t* s = nullptr ) \
    : _base( n, p, s ? s : _spell )

// base class with additional optional arguments
#define DRUID_ABILITY_B( _class, _base, _name, _spell, ... ) \
  _class( druid_t* p, std::string_view n = _name, const spell_data_t* s = nullptr ) \
    : _base( n, p, s ? s : _spell, __VA_ARGS__ )

// child class with additional optional arguments
#define DRUID_ABILITY_C( _class, _base, _name, _spell, ... ) \
  _class( druid_t* p, std::string_view n = _name, const spell_data_t* s = nullptr, __VA_ARGS__ ) \
    : _base( n, p, s ? s : _spell )

template <typename T, typename... Ts>
T* druid_t::get_secondary_action( std::string_view n, Ts&&... args )
{
  auto it = range::find( secondary_action_list, n, &action_t::name_str );
  if ( it != secondary_action_list.cend() )
    return dynamic_cast<T*>( *it );

  T* a;

  if constexpr ( std::is_constructible_v<T, druid_t*, std::string_view, Ts...> )
    a = new T( this, n, std::forward<Ts>( args )... );
  else
    a = new T( this, std::forward<Ts>( args )... );

  secondary_action_list.push_back( a );
  return a;
}

namespace spells
{
/* druid_spell_t ============================================================
  Early definition of druid_spell_t. Only spells that MUST for use by other
  actions should go here, otherwise they can go in the second spells
  namespace.
========================================================================== */
struct druid_spell_t : public druid_spell_base_t<spell_t>
{
private:
  using ab = druid_spell_base_t<spell_t>;

public:
  druid_spell_t( std::string_view n, druid_t* p, const spell_data_t* s = spell_data_t::nil() ) : ab( n, p, s ) {}
};  // end druid_spell_t

// Form Spells ==============================================================
struct druid_form_t : public druid_spell_t
{
  form_e form;

  druid_form_t( std::string_view n, druid_t* p, const spell_data_t* s, form_e f )
    : druid_spell_t( n, p, s ), form( f )
  {
    harmful = may_autounshift = reset_melee_swing = false;
    ignore_false_positive = true;

    form_mask = ( NO_FORM | BEAR_FORM | CAT_FORM | MOONKIN_FORM ) & ~form;
  }

  void execute() override
  {
    druid_spell_t::execute();

    p()->shapeshift( form );
  }
};

// Bear Form Spell ==========================================================
struct bear_form_t : public druid_form_t
{
  DRUID_ABILITY_B( bear_form_t, druid_form_t, "bear_form", p->find_class_spell( "Bear Form" ), BEAR_FORM)
  {}

  void execute() override
  {
    druid_form_t::execute();
  }
};

// Cat Form Spell ===========================================================
struct cat_form_t : public druid_form_t
{
  DRUID_ABILITY_B( cat_form_t, druid_form_t, "cat_form", p->find_class_spell( "Cat Form" ), CAT_FORM )
  {}
};

// Moonkin Form Spell =======================================================
struct moonkin_form_t : public druid_form_t
{
  DRUID_ABILITY_B( moonkin_form_t, druid_form_t, "moonkin_form", p->talent.moonkin_form, MOONKIN_FORM )
  {}
};

// Cancelform (revert to caster form)========================================
struct cancel_form_t : public druid_form_t
{
  DRUID_ABILITY_B( cancel_form_t, druid_form_t, "cancelform", spell_data_t::nil(), NO_FORM )
  {
    trigger_gcd = 0_ms;
    use_off_gcd = true;
  }
};
}  // namespace spells

namespace heals
{
// ==========================================================================
// Druid Heal
// ==========================================================================
struct druid_heal_t : public druid_spell_base_t<heal_t>
{
  double master_ss_mul;   // % healing from master shapeshifter
  bool target_self = false;

  druid_heal_t( std::string_view n, druid_t* p, const spell_data_t* s = spell_data_t::nil() )
    : base_t( n, p, s ),
      master_ss_mul( p->talent.master_shapeshifter->effectN(1).percent() )
  {
    add_option( opt_bool( "target_self", target_self ) );

    if ( target_self )
      target = p;

    may_miss = harmful = false;
    ignore_false_positive = true;
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    double ctm = base_t::composite_target_multiplier( t );

    ctm *= master_ss_mul;

    return ctm;
  }

  void impact( action_state_t* s ) override
  {
    base_t::impact( s );

    if ( s->result_type == result_amount_type::HEAL_DIRECT )
    {
      p()->buff.harmony->trigger(1, p()->mastery.harmony->effectN(1).percent() * p()->composite_mastery());
    }
  }

};
}  // namespace heals

namespace cat_attacks
{
// ==========================================================================
// Druid Cat Attack
// ==========================================================================
struct cat_attack_t : public druid_attack_t<melee_attack_t>
{
protected:
  bool attack_critical;

public:
  std::vector<buff_effect_t> persistent_multiplier_buffeffects;

  struct
  {
    bool tigers_fury;
    bool clearcasting;
  } snapshots;

  snapshot_counter_t* tf_counter = nullptr;

  double primal_fury_cp;

  cat_attack_t( std::string_view n, druid_t* p, const spell_data_t* s = spell_data_t::nil() )
    : base_t( n, p, s ),
      snapshots(),
      primal_fury_cp( find_effect( find_trigger( p->talent.primal_fury ).trigger(), E_ENERGIZE ).resource( RESOURCE_COMBO_POINT ) )
  {

    if ( data().ok() )
    {
      snapshots.tigers_fury =
          parse_persistent_buff_effects( p->buff.tigers_fury, 0U, true );

      snapshots.clearcasting =
          parse_persistent_buff_effects( p->buff.clearcasting, 0U, false );

      parse_passive_effects( p->mastery.razor_claws );
    }
  }

  virtual bool stealthed() const  // For effects that require any form of stealth.
  {
    // Make sure we call all for accurate benefit tracking. Berserk/Incarn/Sudden Assault handled in shred_t & rake_t -
    // move here if buff while stealthed becomes more widespread
    return p()->buff.prowl->up() || p()->buffs.shadowmeld->up();
  }

  void consume_resource() override
  {
    double eff_cost = base_cost();

    base_t::consume_resource();

    // Treat Omen of Clarity energy savings like an energy gain for tracking purposes.
    if ( snapshots.clearcasting && current_resource() == RESOURCE_ENERGY &&
         p()->buff.clearcasting->up() )
    {
      p()->buff.clearcasting->decrement();

      // Base cost doesn't factor in but Omen of Clarity does net us less energy during it, so account for that here.
      eff_cost *= 1.0 + p()->buff.berserk->check_value();

      p()->gain.clearcasting->add( RESOURCE_ENERGY, eff_cost );
    }
  }

  template <typename... Ts>
  bool parse_persistent_buff_effects( buff_t* buff, unsigned ignore_mask, bool use_stacks, Ts... mods )
  {
    size_t ta_old   = ta_multiplier_buffeffects.size();
    size_t da_old   = da_multiplier_buffeffects.size();
    size_t cost_old = cost_buffeffects.size();

    parse_buff_effects( buff, ignore_mask, use_stacks, USE_DATA, mods... );

    // If there is a new entry in the ta_mul table, move it to the pers_mul table.
    if ( ta_multiplier_buffeffects.size() > ta_old )
    {
      double &ta_val = ta_multiplier_buffeffects.back().value;
      double da_val = 0;

      // Any corresponding increases in the da_mul table can be removed as pers_mul covers da_mul & ta_mul
      if ( da_multiplier_buffeffects.size() > da_old )
      {
        da_val = da_multiplier_buffeffects.back().value;
        da_multiplier_buffeffects.pop_back();

        if ( da_val != ta_val )
        {
          p()->sim->print_debug(
              "WARNING: {} (id={}) spell data has inconsistent persistent multiplier benefit for {}.", buff->name(),
              buff->data().id(), this->name() );
        }
      }

      // snapshotting is done via custom scripting, so data can have different da/ta_mul values but the highest will apply
      if ( da_val > ta_val )
        ta_val = da_val;

      persistent_multiplier_buffeffects.push_back( ta_multiplier_buffeffects.back() );
      ta_multiplier_buffeffects.pop_back();

      p()->sim->print_debug(
          "persistent-buffs: {} ({}) damage modified by {}% with buff {} ({}), tick table has {} entries.", name(), id,
          persistent_multiplier_buffeffects.back().value * 100.0, buff->name(), buff->data().id(),
          ta_multiplier_buffeffects.size() );

      return true;
    }
    // no persistent multiplier, but does snapshot & consume the buff
    if ( da_multiplier_buffeffects.size() > da_old || cost_buffeffects.size() > cost_old )
      return true;

    return false;
  }

  double composite_persistent_multiplier( const action_state_t* s ) const override
  {
    return base_t::composite_persistent_multiplier( s ) * get_buff_effects_value( persistent_multiplier_buffeffects );
  }

  snapshot_counter_t* get_counter( buff_t* buff )
  {
    auto st = stats;
    while ( st->parent )
    {
      if ( !st->parent->action_list.front()->harmful )
        return nullptr;
      st = st->parent;
    }

    for ( const auto& c : p()->counters )
      if ( c->stats == st)
        for ( const auto& b : c->buffs )
          if ( b == buff )
            return c.get();

    return p()->counters.emplace_back( std::make_unique<snapshot_counter_t>( p(), st, buff ) ).get();
  }

  void init() override
  {
    base_t::init();

    if ( !is_auto_attack && !data().ok() )
      return;

    if ( snapshots.tigers_fury )
      tf_counter = get_counter( p()->buff.tigers_fury );
  }

  void impact( action_state_t* s ) override
  {
    base_t::impact( s );

    if ( result_is_hit( s->result ) )
    {
      if ( s->result == RESULT_CRIT )
        attack_critical = true;
    }
  }

  void tick( dot_t* d ) override
  {
    base_t::tick( d );

    if ( snapshots.tigers_fury && tf_counter )
      tf_counter->count_tick();
  }

  void execute() override
  {
    attack_critical = false;

    base_t::execute();

    if ( energize_resource == RESOURCE_COMBO_POINT && energize_amount > 0 && hit_any_target )
    {
      trigger_primal_fury();
    }

    if ( hit_any_target )
    {
      if ( snapshots.tigers_fury && tf_counter )
        tf_counter->count_execute();
    }

    if ( !hit_any_target )
      trigger_energy_refund();

    if ( harmful )
    {
      if ( special && base_costs[ RESOURCE_ENERGY ] > 0 )
        p()->buff.berserk->up();
    }
  }

  void trigger_energy_refund()
  {
    player->resource_gain( RESOURCE_ENERGY, last_resource_cost * 0.80, p()->gain.energy_refund );
  }

  virtual void trigger_primal_fury()
  {
    if ( proc || !p()->talent.primal_fury.ok() || !attack_critical )
      return;

    p()->proc.primal_fury->occur();
    p()->resource_gain( RESOURCE_COMBO_POINT, primal_fury_cp, p()->gain.primal_fury );
  }

  size_t total_buffeffects_count() override
  {
    return base_t::total_buffeffects_count() + persistent_multiplier_buffeffects.size();
  }

  void print_parsed_custom_type( report::sc_html_stream& os ) override
  {
    print_parsed_type( os, persistent_multiplier_buffeffects, "Snapshots" );
  }


};  // end druid_cat_attack_t

struct cat_finisher_data_t
{
  int combo_points = 0;

  void sc_format_to( const cat_finisher_data_t& data, fmt::format_context::iterator out )
  {
    fmt::format_to( out, "combo_points={}", data.combo_points );
  }
};

template <class Data = cat_finisher_data_t>
struct cp_spender_t : public cat_attack_t
{
protected:
  using base_t = cp_spender_t<Data>;
  using state_t = druid_action_state_t<Data>;

public:
  bool consumes_combo_points = true;

  cp_spender_t( std::string_view n, druid_t* p, const spell_data_t* s ) : cat_attack_t( n, p, s ) {}

  action_state_t* new_state() override
  {
    return new state_t( this, target );
  }

  state_t* cast_state( action_state_t* s )
  {
    return static_cast<state_t*>( s );
  }

  const state_t* cast_state( const action_state_t* s ) const
  {
    return static_cast<const state_t*>( s );
  }

  const int combo_points( const action_state_t* s ) const
  {
    return cast_state( s )->combo_points;
  }

  // used during state snapshot

  virtual int _combo_points() const
  {
      return as<int>( p()->resources.current[ RESOURCE_COMBO_POINT ] );
  }

  void snapshot_state( action_state_t* s, result_amount_type rt ) override
  {
    // snapshot the combo point first so composite_X calculations can correctly refer to it
    cast_state( s )->combo_points = _combo_points();

    cat_attack_t::snapshot_state( s, rt );
  }

  bool ready() override
  {
    if ( consumes_combo_points && p()->resources.current[ RESOURCE_COMBO_POINT ] < 1 )
      return false;

    return cat_attack_t::ready();
  }

  void consume_resource() override
  {
    cat_attack_t::consume_resource();

    if ( background || !hit_any_target || !consumes_combo_points )
      return;

    auto consumed = _combo_points();


    p()->buff.predatory_swiftness->trigger( 1, buff_t::DEFAULT_VALUE(),
                                            consumed * p()->talent.predatory_strikes->effectN( 3 ).percent() );

    p()->resource_loss( RESOURCE_COMBO_POINT, consumed, nullptr, this );

    if ( sim->log )
    {
    sim->print_log( "{} consumes {} {} for {} (0)", player->name(), consumed,
                    util::resource_type_string( RESOURCE_COMBO_POINT ), name() );
    }

    stats->consume_resource( RESOURCE_COMBO_POINT, consumed );
  }
};

using cat_finisher_t = cp_spender_t<>;

// Berserk (Cat) ==============================================================
struct berserk_cat_base_t : public cat_attack_t
{
  buff_t* buff;

  berserk_cat_base_t( std::string_view n, druid_t* p, const spell_data_t* s )
    : cat_attack_t( n, p, s), buff( p->buff.berserk )
  {
    harmful   = false;
    form_mask = CAT_FORM;
  }

  void execute() override
  {
    cat_attack_t::execute();

    buff->extend_duration_or_trigger();
  }
};

struct berserk_cat_t : public berserk_cat_base_t
{
  DRUID_ABILITY( berserk_cat_t, berserk_cat_base_t, "berserk_cat", p->talent.berserk )
  {
    name_str_reporting = "berserk";
  }
};

// Ferocious Bite ===========================================================
struct ferocious_bite_t : public cat_finisher_t
{
  double excess_energy = 0.0;
  double max_excess_energy;
  bool max_energy = false;

  DRUID_ABILITY( ferocious_bite_t, cat_finisher_t, "ferocious_bite", p->find_class_spell( "Ferocious Bite" ) )
  {
    add_option( opt_bool( "max_energy", max_energy ) );

    max_excess_energy = modified_effect( find_effect_index( this, E_DUMMY ) ).base_value();
  }

  double maximum_energy() const
  {
    double req = base_costs[ RESOURCE_ENERGY ] + max_excess_energy;

    req *= 1.0 + p()->buff.berserk->check_value();

    return req;
  }

  bool ready() override
  {
    if ( max_energy && p()->resources.current[ RESOURCE_ENERGY ] < maximum_energy() )
      return false;

    return cat_finisher_t::ready();
  }

  void execute() override
  {
    // Incarn does affect the additional energy consumption.
    double _max_used = max_excess_energy * ( 1.0 + p()->buff.berserk->check_value() );

    excess_energy = std::min( _max_used, ( p()->resources.current[ RESOURCE_ENERGY ] - cat_finisher_t::cost() ) );
    
    cat_finisher_t::execute();
  }

  void consume_resource() override
  {
    // Extra energy consumption happens first. In-game it happens before the skill even casts but let's not do that
    // because its dumb.
    if ( hit_any_target )
    {
      player->resource_loss( current_resource(), excess_energy );
      stats->consume_resource( current_resource(), excess_energy );
    }

    cat_finisher_t::consume_resource();
  }

  double composite_crit_chance() const override
  {
    double c = cat_attack_t::composite_crit_chance();
    if (target->debuffs.bleeding->check())
      c += p()->talent.rend_and_tear->effectN( 2 ).percent();
    return c;
  }

  double composite_da_multiplier( const action_state_t* s ) const override
  {
    auto dam = cat_finisher_t::composite_da_multiplier( s );
    auto energy_mul = 1.0 + ( excess_energy / max_excess_energy );
    // base spell coeff is for 5CP, so we reduce if lower than 5.
    auto combo_mul = combo_points( s ) / p()->resources.max[ RESOURCE_COMBO_POINT ];

    return dam * energy_mul * combo_mul;
  }
};

// Maim =====================================================================
struct maim_t : public cat_finisher_t
{
  DRUID_ABILITY( maim_t, cat_finisher_t, "maim", p->find_class_spell( "Maim" ) ) {}

  double composite_da_multiplier( const action_state_t* s ) const override
  {
    return cat_finisher_t::composite_da_multiplier( s ) * combo_points( s );
  }
};

// Rake =====================================================================
struct rake_t : public cat_attack_t
{
  struct rake_bleed_t : public cat_attack_t
  {
    rake_t* rake = nullptr;

    rake_bleed_t( druid_t* p, std::string_view n, rake_t* r )
      : cat_attack_t( n, p, find_trigger( r ).trigger() ), rake( r )
    {
      background = dual = true;
      // override for convoke. since this is only ever executed from rake_t, form checking is unnecessary.
      form_mask = 0;

      dot_name = "rake";
    }

    // read persistent mul off the parent rake's table for reporting
    void print_parsed_custom_type( report::sc_html_stream& os ) override
    {
      rake->print_parsed_custom_type( os );
    }
  };

  rake_bleed_t* bleed{};

  DRUID_ABILITY( rake_t, cat_attack_t, "rake", p->find_class_spell( "Rake" ) )
  {
    apply_affecting_aura( p->talent.endless_carnage );

    if ( data().ok() )
    {
      bleed        = p->get_secondary_action<rake_bleed_t>( name_str + "_bleed", this );
      bleed->stats = stats;
      bleed->rake  = this;
      stats->action_list.push_back( bleed );
    }

    dot_name = "rake";
  }

  bool has_amount_result() const override { return bleed->has_amount_result(); }

  void execute() override
  {
    cat_attack_t::execute();
  }

  void impact( action_state_t* s ) override
  {
    cat_attack_t::impact( s );

    bleed->snapshot_and_execute( s, true, nullptr, []( const action_state_t* from, action_state_t* to ) {
      // Copy persistent multipliers from the direct attack.
      to->persistent_multiplier = from->persistent_multiplier;
    } );
  }
};

struct pounce_t : public cat_attack_t
{
  struct pounce_bleed_t : public cat_attack_t
  {
    pounce_t* pounce = nullptr;
    pounce_bleed_t( druid_t* p, std::string_view n, const spell_data_t* s ) : cat_attack_t( n, p, s )
    {
      background = true;
      dot_name = "pounce";
    }
    // read persistent mul off the parent pounce's table for reporting
    void print_parsed_custom_type( report::sc_html_stream& os ) override
    {
      pounce->print_parsed_custom_type( os );
    }
  };

  pounce_bleed_t* bleed;
  DRUID_ABILITY( pounce_t, cat_attack_t, "pounce", p->find_class_spell( "Pounce" ) )
  {
    if ( data().ok() )
    {
      bleed         = p->get_secondary_action<pounce_bleed_t>( name_str + "_bleed", find_trigger( this ).trigger() );
      bleed->stats  = stats;
      bleed->pounce = this;
      stats->action_list.push_back( bleed );
    }

    dot_name = "pounce";
  }

  void impact( action_state_t* s ) override
  {
    cat_attack_t::impact( s );

    bleed->snapshot_and_execute( s, true, nullptr, []( const action_state_t* from, action_state_t* to ) {
      // Copy persistent multipliers from the direct attack.
      to->persistent_multiplier = from->persistent_multiplier;
    } );
  }
};

struct ravage_t : public cat_attack_t
{
  DRUID_ABILITY( ravage_t, cat_attack_t, "ravage", p->find_class_spell( "Ravage" ) )
  {
    requires_position = POSITION_BACK;
  }

  double composite_crit_chance() const override
  {
    double c = cat_attack_t::composite_crit_chance();

    if ( p()->talent.predatory_strikes->ok() )
    {
      if ( target->health_percentage() >= p()->talent.predatory_strikes->effectN( 2 ).base_value() )
      {
        c += p()->talent.predatory_strikes->effectN( 1 ).percent();
      }
    }

    return c;
  }

  bool ready() override
  {
    if ( p()->buff.stampede_cat->check() )
    {
      requires_position = POSITION_NONE;
      ignore_stealth    = true;
    }
    else
    {
      requires_position = POSITION_BACK;
      ignore_stealth    = false;
    }
    return cat_attack_t::ready();
  }
};

// Rip ======================================================================
struct rip_t : public cat_finisher_t
{
  DRUID_ABILITY( rip_t, base_t, "rip", p->find_class_spell( "Rip" ) )
  {
    dot_name = "rip";
  }

  timespan_t composite_dot_duration( const action_state_t* s ) const override
  {
    timespan_t t = base_t::composite_dot_duration( s );

    return t *= combo_points( s ) + 1;
  }

  void impact( action_state_t* s ) override
  {
    cat_finisher_t::impact(s);

    if ( result_is_hit( s->result ) && p()->glyphs.bloodletting->ok() )
    {
      p()->buff.rip_extensions = 3;
    }
  }
};

// Shred ====================================================================
struct shred_t : public cat_attack_t
{
  DRUID_ABILITY( shred_t, cat_attack_t, "shred", p->find_class_spell( "Shred" ) )
  {
    bleed_mul = p->talent.rend_and_tear->effectN( 1 ).percent();
    requires_position = POSITION_BACK;
  }

  double composite_target_multiplier( player_t* t ) const override
  {
    double ctm = cat_attack_t::composite_target_multiplier( t );

    if (t->debuffs.bleed_dmg_taken && t->debuffs.bleed_dmg_taken->up())
      ctm *= 1 + t->debuffs.bleed_dmg_taken->check_value();
    return ctm;
  }
};

// Swipe (Cat) ====================================================================
struct swipe_cat_t : public cat_attack_t
{
  DRUID_ABILITY( swipe_cat_t, cat_attack_t, "swipe_cat", p->find_class_spell( "Swipe (Cat)" ) )
  {
    aoe = -1;
    reduced_aoe_targets = data().effectN( 4 ).base_value();

    if ( p->specialization() == DRUID_FERAL )
      name_str_reporting = "swipe";
  }
};

// Tiger's Fury =============================================================
struct tigers_fury_t : public cat_attack_t
{
  DRUID_ABILITY( tigers_fury_t, cat_attack_t, "tigers_fury", p->find_class_spell( "Tiger's Fury" ) )
  {
    harmful = false;
    energize_type = action_energize::ON_CAST;
    track_cd_waste = true;

    form_mask = CAT_FORM;
    autoshift = p->active.shift_to_cat;
  }

  void execute() override
  {
    cat_attack_t::execute();

    p()->buff.tigers_fury->trigger();

    if (p()->talent.king_of_the_jungle->ok())
    {
      p()->resource_gain( RESOURCE_ENERGY, p()->talent.king_of_the_jungle->effectN( 2 ).base_value() );
    }
  }

  bool ready() override
  {
    if ( p()->buff.berserk->check() )
      return false;
    return cat_attack_t::ready();
  }
};
}  // end namespace cat_attacks

namespace bear_attacks
{
// ==========================================================================
// Druid Bear Attack
// ==========================================================================
struct bear_attack_t : public druid_attack_t<melee_attack_t>
{
  bear_attack_t( std::string_view n, druid_t* p, const spell_data_t* s = spell_data_t::nil() )
    : base_t( n, p, s )
  {
    if ( p->specialization() == DRUID_BALANCE || p->specialization() == DRUID_RESTORATION )
      ap_type = attack_power_type::NO_WEAPON;
  }
};  // end bear_attack_t

template <typename BASE = bear_attack_t>
struct rage_spender_t : public BASE
{
private:
  druid_t* p_;

protected:
  using base_t = rage_spender_t<BASE>;

public:
  rage_spender_t( std::string_view n, druid_t* p, const spell_data_t* s )
    : BASE( n, p, s ),
      p_( p )
  {}

  void consume_resource() override
  {
    BASE::consume_resource();

    if ( !BASE::last_resource_cost )
      return;
  }
};

// Berserk (Bear) ===========================================================
struct berserk_bear_base_t : public bear_attack_t
{
  buff_t* buff;

  berserk_bear_base_t( std::string_view n, druid_t* p, const spell_data_t* s )
    : bear_attack_t( n, p, s ), buff( p->buff.berserk )
  {
    harmful   = false;
    form_mask = BEAR_FORM;
  }

  void execute() override
  {
    bear_attack_t::execute();

    buff->trigger();

    p()->cooldown.mangle->reset( true );
  }
};

struct berserk_bear_t : public berserk_bear_base_t
{
  DRUID_ABILITY( berserk_bear_t, berserk_bear_base_t, "berserk_bear", p->talent.berserk )
  {
    name_str_reporting = "berserk";
  }
};

// Growl ====================================================================
struct growl_t : public bear_attack_t
{
  DRUID_ABILITY( growl_t, bear_attack_t, "growl", p->find_class_spell( "Growl" ) )
  {
    ignore_false_positive = use_off_gcd = true;
    may_miss = may_parry = may_dodge = may_block = false;
  }

  void impact( action_state_t* s ) override
  {
    if ( s->target->is_enemy() )
      target->taunt( player );

    bear_attack_t::impact( s );
  }
};

// Mangle ===================================================================
struct mangle_t : public bear_attack_t
{
  struct swiping_mangle_t : public druid_residual_action_t<bear_attack_t>
  {
    swiping_mangle_t( druid_t* p, std::string_view n ) : base_t( n, p, p->find_spell( 395942 ) )
    {
      auto set_ = p->sets->set( DRUID_GUARDIAN, T29, B2 );

      aoe = -1;
      reduced_aoe_targets = set_->effectN( 2 ).base_value();
      name_str_reporting = "swiping_mangle";

      residual_mul = set_->effectN( 1 ).percent();
    }

    std::vector<player_t*>& target_list() const override
    {
      target_cache.is_valid = false;

      std::vector<player_t*>& tl = base_t::target_list();

      tl.erase( std::remove( tl.begin(), tl.end(), target ), tl.end() );

      return tl;
    }
  };

  swiping_mangle_t* swiping = nullptr;
  action_t* healing = nullptr;
  int inc_targets = 0;

  DRUID_ABILITY( mangle_t, bear_attack_t, "mangle", p->spec.mangle )
  {
    track_cd_waste = true;

    energize_amount = modified_effect( find_effect_index( this, E_ENERGIZE ) ).resource( RESOURCE_RAGE );

    if ( p->sets->has_set_bonus( DRUID_GUARDIAN, T29, B2 ) )
    {
      auto suf = get_suffix( name_str, "mangle" );
      swiping = p->get_secondary_action<swiping_mangle_t>( "swiping_mangle" + suf );
      swiping->background = true;
      add_child( swiping );
    }

  }

  int n_targets() const override
  {
    auto n = bear_attack_t::n_targets();

    if ( p()->buff.berserk->check() )
      n += inc_targets;

    return n;
  }
};

// Maul =====================================================================
struct maul_t : public rage_spender_t<>
{
  DRUID_ABILITY( maul_t, base_t, "maul", p->find_class_spell( "Maul" ) ) {}

  double composite_target_multiplier( player_t* t ) const override
  {
    double ctm = rage_spender_t::composite_target_multiplier( t );

    if ( t->debuffs.bleed_dmg_taken && t->debuffs.bleed_dmg_taken->up() )
      ctm *= 1 + t->debuffs.bleed_dmg_taken->check_value();
    return ctm;
  }
};

struct enrage_t : public bear_attack_t
{
  DRUID_ABILITY( enrage_t, bear_attack_t, "enrage", p->find_class_spell( "Enrage" ) ) {}

  void execute() override
  {
    bear_attack_t::execute();
    p()->buff.enrage->trigger();
    if ( p()->talent.primal_madness->ok() )
    {
      p()->resource_gain( RESOURCE_RAGE, p()->talent.primal_madness->effectN( 1 ).resource( RESOURCE_RAGE ),
                          p()->gain.primal_madness ); 
    }
  }
};

// Pulverize ================================================================
struct pulverize_t : public bear_attack_t
{
  int consume;

  DRUID_ABILITY( pulverize_t, bear_attack_t, "pulverize", p->find_class_spell( "Pulverize" ) ),
      consume( as<int>( data().effectN( 3 ).base_value() ) )
  {}

  void impact( action_state_t* s ) override
  {
    bear_attack_t::impact( s );

    if ( !result_is_hit( s->result ) )
      return;

    p()->buff.pulverize->trigger();
  }
};

struct thrash_t : public bear_attack_t
{
  DRUID_ABILITY( thrash_t, bear_attack_t, "thrash", p->find_class_spell( "Thrash" ) )
  {}
};

// Swipe (Bear) =============================================================
struct swipe_bear_t : public bear_attack_t
{
  DRUID_ABILITY( swipe_bear_t, bear_attack_t, "swipe_bear", p->find_class_spell( "Swipe (Bear)" ) )
  {
    aoe = -1;
    // target hit data stored in cat swipe
    //reduced_aoe_targets = p->apply_override( p->spec.swipe, p->spec.cat_form_override )->effectN( 4 ).base_value();

    if ( p->specialization() == DRUID_GUARDIAN )
      name_str_reporting = "swipe";
  }
};
} // end namespace bear_attacks

namespace heals
{
// Frenzied Regeneration ====================================================
struct frenzied_regeneration_t : public bear_attacks::rage_spender_t<druid_heal_t>
{
  cooldown_t* dummy_cd;
  cooldown_t* orig_cd;

  DRUID_ABILITY( frenzied_regeneration_t, base_t, "frenzied_regeneration",
      p->find_class_spell( "Frenzied Regeneration" ) ),
      dummy_cd( p->get_cooldown( "dummy_cd" ) ),
      orig_cd( cooldown )
  {
    target = p;
  }
};

// Lifebloom ================================================================
struct lifebloom_t : public druid_heal_t
{
  struct lifebloom_bloom_t : public druid_heal_t
  {
    lifebloom_bloom_t( druid_t* p ) : druid_heal_t( "lifebloom_bloom", p, p->find_spell( 33778 ) )
    {
      background = dual = true;
    }
  };

  lifebloom_bloom_t* bloom;

  DRUID_ABILITY( lifebloom_t, druid_heal_t, "lifebloom", p->find_class_spell( "Lifebloom" ) )
  {
    if ( data().ok() )
    {
      bloom        = p->get_secondary_action<lifebloom_bloom_t>( "lifebloom_bloom" );
      bloom->stats = stats;
      stats->action_list.push_back( bloom );
    }
  }

  void impact( action_state_t* s ) override
  {
    // Cancel dot on all targets other than the one we impact on
    for ( const auto& t : sim->player_non_sleeping_list )
    {
      if ( t == target )
        continue;

      auto d = get_dot( t );

      if ( sim->debug )
        sim->print_debug( "{} fades from {}", *d, *t );

      d->reset();  // we don't want last_tick() because there is no bloom on target swap
    }

    auto lb = get_dot( target );
    if ( lb->is_ticking() && lb->remains() <= dot_duration * 0.3 )
      bloom->execute_on_target( target );

    druid_heal_t::impact( s );
  }

  void trigger_dot( action_state_t* s ) override
  {
    auto lb = find_dot( s->target );
    if ( lb && lb->remains() <= composite_dot_duration( lb->state ) * 0.3 )
      bloom->execute_on_target( s->target );

    druid_heal_t::trigger_dot( s );
  }

  void tick( dot_t* d ) override
  {
    druid_heal_t::tick( d );

    // todo: trigger malfurions gift
    //p()->buff.clearcasting_tree->trigger();
  }

  void last_tick( dot_t* d ) override
  {
    if ( !d->state->target->is_sleeping() )  // Prevent crash at end of simulation
      bloom->execute_on_target( d->target );

    druid_heal_t::last_tick( d );
  }
};

// Nature's Swiftness =======================================================
struct natures_swiftness_t : public druid_heal_t
{
  DRUID_ABILITY( natures_swiftness_t, druid_heal_t, "natures_swiftness", p->talent.natures_swiftness )
  {}

  timespan_t cooldown_duration() const override
  {
    return 0_ms;  // cooldown handled by buff.natures_swiftness
  }

  void execute() override
  {
    druid_heal_t::execute();

    p()->buff.natures_swiftness->trigger();
  }

  bool ready() override
  {
    if ( p()->buff.natures_swiftness->check() )
      return false;

    return druid_heal_t::ready();
  }
};

// Nourish ==================================================================
struct nourish_t : public druid_heal_t
{
  DRUID_ABILITY( nourish_t, druid_heal_t, "nourish", p->find_class_spell( "Nourish" ) ) {}

  double composite_target_multiplier( player_t* t ) const override
  {
    double ctm = druid_heal_t::composite_target_multiplier( t );

    if ( td( t )->hots_ticking() )
      ctm *= 1.20;
    return ctm;
  }
};

// Regrowth =================================================================
struct regrowth_t : public druid_heal_t
{
  DRUID_ABILITY( regrowth_t, druid_heal_t, "regrowth", p->find_class_spell( "Regrowth" ) )
  {
    form_mask = NO_FORM | MOONKIN_FORM;
    may_autounshift = true;
  }

  timespan_t execute_time() const override
  {
    if ( p()->buff.tree_of_life->check() )
      return 0_ms;

    return druid_heal_t::execute_time();
  }

  void execute() override
  {
    druid_heal_t::execute();

    p()->buff.predatory_swiftness->expire();

    p()->buff.natures_swiftness->expire();
  }
};

// Rejuvenation =============================================================
struct rejuvenation_base_t : public druid_heal_t
{
  rejuvenation_base_t( std::string_view n, druid_t* p, const spell_data_t* s )
    : druid_heal_t( n, p, s )
  {
    apply_affecting_aura( p->talent.improved_rejuvenation );
  }
};

struct rejuvenation_t : public rejuvenation_base_t
{
  DRUID_ABILITY( rejuvenation_t, rejuvenation_base_t, "rejuvenation", p->find_class_spell( "Rejuvenation" ) )
  {}
};

// Swiftmend ================================================================
struct swiftmend_t : public druid_heal_t
{
  DRUID_ABILITY( swiftmend_t, druid_heal_t, "swiftmend", p->spec.swiftmend ) {}

  bool target_ready( player_t* t ) override
  {
    auto hots = td( t )->hots;

    if ( hots.regrowth->is_ticking() || hots.rejuvenation->is_ticking() )
      return druid_heal_t::target_ready( t );

    return false;
  }

  void impact( action_state_t* s ) override
  {
    auto t_td = td( s->target );

    if ( t_td->hots.regrowth->is_ticking() )
      t_td->hots.regrowth->cancel();
    else if ( t_td->hots.rejuvenation->is_ticking() )
      t_td->hots.rejuvenation->cancel();
    else
      sim->error( "Swiftmend impact with no HoT ticking" );

    druid_heal_t::impact( s );
  }
};

// Tranquility ==============================================================
struct tranquility_t : public druid_heal_t
{
  struct tranquility_tick_t : public druid_heal_t
  {
    tranquility_tick_t( druid_t* p )
      : druid_heal_t( "tranquility_tick", p, find_trigger( p->find_class_spell( "Tranquility" ) ).trigger() )
    {
      background = dual = true;
      aoe = -1;
    }
  };

  DRUID_ABILITY( tranquility_t, druid_heal_t, "tranquility", p->find_class_spell( "Tranquility" ) )
  {
    channeled = true;

    tick_action = p->get_secondary_action<tranquility_tick_t>( "tranquility_tick" );
  }

  void init() override
  {
    druid_heal_t::init();

    // necessary because action_t::init() sets all tick_action::direct_tick to true
    tick_action->direct_tick = false;
  }
};

// Wild Growth ==============================================================
// The spellpower coefficient c of a tick of WG is given by:
//   c(t) = 0.175 - 0.07 * t / D
// where:
//   t = time of tick = current_time - start_time
//   D = full duration of WG
struct wild_growth_t : public druid_heal_t
{
  double decay_coeff{ 0.0 };
  int inc_mod{ 0 };

  DRUID_ABILITY( wild_growth_t, druid_heal_t, "wild_growth", p->talent.wild_growth ),
      //decay_coeff( 0.07 * ( 1.0 - p->talent.unstoppable_growth->effectN( 1 ).percent() ) ),
      inc_mod( as<int>( p->talent.tree_of_life->effectN( 3 ).base_value() ) )
  {
    aoe = as<int>( modified_effect( 2 ).base_value() );

    // '0-tick' coeff, also unknown if this is hard set to 0.175 or based on a formula as below
    spell_power_mod.tick += decay_coeff / 2.0;
  }

  double spell_tick_power_coefficient( const action_state_t* s ) const override
  {
    auto c = druid_heal_t::spell_tick_power_coefficient( s );
    auto dot = find_dot( s->target );

    c -= decay_coeff * ( 1.0 - dot->remains() / dot->duration() );

    return c;
  }

  int n_targets() const override
  {
    int n = druid_heal_t::n_targets();

    if ( n && p()->buff.tree_of_life->check() )
        n += inc_mod;

    return n;
  }
};

}  // end namespace heals

template <class Base>
void druid_action_t<Base>::init()
{
  ab::init();

  if ( !ab::harmful && !dynamic_cast<heals::druid_heal_t*>( this ) )
    ab::target = ab::player;
}

template <class Base>
void druid_action_t<Base>::schedule_execute( action_state_t* s )
{
  check_autoshift();

  ab::schedule_execute( s );
}

template <class Base>
void druid_action_t<Base>::execute()
{
  ab::execute();

  if ( ab::use_off_gcd )
    check_autoshift();

  if ( break_stealth )
  {
    p()->buff.prowl->expire();
    p()->buffs.shadowmeld->expire();
  }
}

namespace spells
{
// ==========================================================================
// Druid Spells
// ==========================================================================

// Barkskin =================================================================
struct barkskin_t : public druid_spell_t
{
  DRUID_ABILITY( barkskin_t, druid_spell_t, "barkskin", p->find_class_spell( "Barkskin" ) )
  {
    harmful = false;
    use_off_gcd = true;
    dot_duration = 0_ms;
  }

  void execute() override
  {
    druid_spell_t::execute();

    p()->buff.barkskin->trigger();
  }
};

// Force of Nature ==========================================================
struct force_of_nature_t : public druid_spell_t
{
  unsigned num;

  DRUID_ABILITY( force_of_nature_t, druid_spell_t, "force_of_nature", p->talent.force_of_nature ),
      num( as<unsigned>( p->talent.force_of_nature->effectN( 1 ).base_value() ) )
  {
    harmful = false;

    p->pets.force_of_nature.set_default_duration( find_trigger( p->talent.force_of_nature ).trigger()->duration() + 1_ms );
    p->pets.force_of_nature.set_creation_event_callback( pets::parent_pet_action_fn( this ) );
  }

  void execute() override
  {
    druid_spell_t::execute();

    p()->pets.force_of_nature.spawn( num );
  }
};

// Incarnation (Tree) =========================================================
struct tree_of_life_t : public druid_spell_t
{
  DRUID_ABILITY( tree_of_life_t, druid_spell_t, "tree_of_life", p->talent.tree_of_life )
  {
    harmful   = false;
    form_mask = NO_FORM;
    autoshift = p->active.shift_to_caster;
  }

  void execute() override
  {
    druid_spell_t::execute();

    p()->buff.tree_of_life->trigger();
  }
};

// Innervate ================================================================
struct innervate_t : public druid_spell_t
{
  DRUID_ABILITY( innervate_t, druid_spell_t, "innervate", p->find_class_spell( "Innervate" ) )
  {
    harmful = false;
  }

  void execute() override
  {
    druid_spell_t::execute();

    p()->buff.innervate->trigger();
  }
};

// Mark of the Wild =========================================================
struct mark_of_the_wild_t : public druid_spell_t
{
  DRUID_ABILITY( mark_of_the_wild_t, druid_spell_t, "mark_of_the_wild", p->find_class_spell( "Mark of the Wild" ) )
  {
    harmful = false;
    ignore_false_positive = true;

    if ( sim->overrides.mark_of_the_wild )
      background = true;
  }

  void execute() override
  {
    druid_spell_t::execute();

    if ( !sim->overrides.mark_of_the_wild )
      sim->auras.mark_of_the_wild->trigger();
  }
};

struct prowl_t : public druid_spell_t
{
  DRUID_ABILITY( prowl_t, druid_spell_t, "prowl", p->find_class_spell( "Prowl" ) )
  {
    use_off_gcd = ignore_false_positive = true;
    harmful = break_stealth = false;

    form_mask = CAT_FORM;
    autoshift = p->active.shift_to_cat;
  }

  void execute() override
  {
    if ( sim->log )
      sim->print_log( "{} performs {}", player->name(), name() );

    p()->buff.prowl->trigger();

    druid_spell_t::execute();

    p()->cancel_auto_attacks();

    if ( !p()->in_boss_encounter )
      p()->leave_combat();
  }

  bool ready() override
  {
    if ( p()->buff.prowl->check() || ( p()->in_combat ) )
      return false;

    return druid_spell_t::ready();
  }
};

// Proxy Swipe Spell ========================================================
struct swipe_proxy_t : public druid_spell_t
{
  action_t* swipe_cat;
  action_t* swipe_bear;

  swipe_proxy_t( druid_t* p )
    : druid_spell_t( "swipe", p, p->find_class_spell( "Swipe (Cat") ),
      swipe_cat( new cat_attacks::swipe_cat_t( p ) ),
      swipe_bear( new bear_attacks::swipe_bear_t( p ) )
  {}

  void parse_options( util::string_view opt ) override
  {
    druid_spell_t::parse_options( opt );

    swipe_cat->parse_options( opt );
    swipe_bear->parse_options( opt );
  }

  timespan_t gcd() const override
  {
    if ( p()->buff.cat_form->check() )
      return swipe_cat->gcd();
    else if ( p()->buff.bear_form->check() )
      return swipe_bear->gcd();

    return druid_spell_t::gcd();
  }

  void execute() override
  {
    if ( p()->buff.cat_form->check() )
      swipe_cat->execute();
    else if ( p()->buff.bear_form->check() )
      swipe_bear->execute();

    if ( pre_execute_state )
      action_state_t::release( pre_execute_state );
  }

  bool action_ready() override
  {
    if ( p()->buff.cat_form->check() )
      return swipe_cat->action_ready();
    else if ( p()->buff.bear_form->check() )
      return swipe_bear->action_ready();

    return false;
  }

  bool target_ready( player_t* candidate_target ) override
  {
    if ( p()->buff.cat_form->check() )
      return swipe_cat->target_ready( candidate_target );
    else if ( p()->buff.bear_form->check() )
      return swipe_bear->target_ready( candidate_target );

    return false;
  }

  bool ready() override
  {
    if ( p()->buff.cat_form->check() )
      return swipe_cat->ready();
    else if ( p()->buff.bear_form->check() )
      return swipe_bear->ready();

    return false;
  }

  double cost() const override
  {
    if ( p()->buff.cat_form->check() )
      return swipe_cat->cost();
    else if ( p()->buff.bear_form->check() )
      return swipe_bear->cost();

    return 0;
  }
};

// Stampeding Roar ==========================================================
struct stampeding_roar_t : public druid_spell_t
{
  DRUID_ABILITY( stampeding_roar_t, druid_spell_t, "stampeding_roar", p->find_class_spell( "Stampeding Roar" ) )
  {
    harmful = false;

    form_mask = BEAR_FORM | CAT_FORM;
    autoshift = p->active.shift_to_bear;
  }

  void execute() override
  {
    druid_spell_t::execute();
  }
};

// Survival Instincts =======================================================
struct survival_instincts_t : public druid_spell_t
{
  DRUID_ABILITY( survival_instincts_t, druid_spell_t, "survival_instincts", p->talent.survival_instincts )
  {
    harmful     = false;
    use_off_gcd = true;
  }

  void execute() override
  {
    druid_spell_t::execute();

    p()->buff.survival_instincts->trigger();
  }
};
}  // end namespace spells

#undef DRUID_ABILITY
#undef DRUID_ABILITY_B
#undef DRUID_ABILITY_C
#undef DRUID_ABILITY_D

namespace auto_attacks
{
template <typename Base>
struct druid_melee_t : public Base
{
  using ab = Base;
  using base_t = druid_melee_t<Base>;

  double ooc_chance = 3.5;

  druid_melee_t( std::string_view n, druid_t* p ) : Base( n, p )
  {
    ab::may_crit = ab::may_glance = ab::background = ab::repeating = ab::is_auto_attack = true;
    ab::allow_class_ability_procs = ab::not_a_proc = true;

    ab::school = SCHOOL_PHYSICAL;
    ab::trigger_gcd = 0_ms;
    ab::special = false;
    ab::weapon_multiplier = 1.0;

    ab::apply_buff_effects();
    ab::apply_debuffs_effects();

    // Auto attack mods
    ab::parse_passive_effects( p->spec_spell );

  }

  timespan_t execute_time() const override
  {
    if ( !ab::player->in_combat )
      return 10_ms;

    return ab::execute_time();
  }

  void impact( action_state_t* s ) override
  {
    ab::impact( s );

    if ( ooc_chance && ab::result_is_hit( s->result ) )
    {
      int active = ab::p()->buff.clearcasting->check();
      double chance = ab::weapon->proc_chance_on_swing( ooc_chance );

      // Internal cooldown is handled by buff.
      if ( ab::p()->buff.clearcasting->trigger( 1, buff_t::DEFAULT_VALUE(), chance ) )
      {
        ab::p()->proc.clearcasting->occur();

        for ( int i = 0; i < active; i++ )
          ab::p()->proc.clearcasting_wasted->occur();
      }
    }
  }
};

// Caster Melee Attack ======================================================
struct caster_melee_t : public druid_melee_t<druid_attack_t<melee_attack_t>>
{
  caster_melee_t( druid_t* p ) : base_t( "caster_melee", p ) {}
};

// Bear Melee Attack ========================================================
struct bear_melee_t : public druid_melee_t<bear_attacks::bear_attack_t>
{
  bear_melee_t( druid_t* p ) : base_t( "bear_melee", p )
  {
    form_mask = form_e::BEAR_FORM;

    energize_type = action_energize::ON_HIT;
    energize_resource = resource_e::RESOURCE_RAGE;
    energize_amount = 4.0;
  }

  void execute() override
  {
    base_t::execute();
  }
};

// Cat Melee Attack =========================================================
struct cat_melee_t : public druid_melee_t<cat_attacks::cat_attack_t>
{
  cat_melee_t( druid_t* p ) : base_t( "cat_melee", p )
  {
    form_mask = form_e::CAT_FORM;

    snapshots.tigers_fury = true;
  }
};

// Auto Attack (Action)========================================================
struct auto_attack_t : public melee_attack_t
{
  auto_attack_t( druid_t* player ) : melee_attack_t( "auto_attack", player, spell_data_t::nil() )
  {
    trigger_gcd = 0_ms;
    ignore_false_positive = use_off_gcd = true;
  }

  void execute() override
  {
    player->main_hand_attack->weapon = &( player->main_hand_weapon );
    player->main_hand_attack->base_execute_time = player->main_hand_weapon.swing_time;
    player->main_hand_attack->schedule_execute();
  }

  bool ready() override
  {
    if ( player->is_moving() )
      return false;

    if ( !player->main_hand_attack )
      return false;

    return ( player->main_hand_attack->execute_event == nullptr );  // not swinging
  }
};
}  // namespace auto_attacks

// ==========================================================================
// Druid Helper Functions & Structures
// ==========================================================================

// ==========================================================================
// Druid Character Definition
// ==========================================================================

// druid_t::activate ========================================================
void druid_t::activate()
{
  player_t::activate();
}

// druid_t::create_action  ==================================================
action_t* druid_t::create_action( std::string_view name, std::string_view opt )
{
  using namespace auto_attacks;
  using namespace cat_attacks;
  using namespace bear_attacks;
  using namespace heals;
  using namespace spells;

  action_t* a = nullptr;

  // Baseline Abilities
  if ( name == "auto_attack"           ) a = new           auto_attack_t( this );
  else if ( name == "bear_form"             ) a = new             bear_form_t( this );
  else if ( name == "cat_form"              ) a = new              cat_form_t( this );
  else if ( name == "cancelform"            ) a = new           cancel_form_t( this );
  else if ( name == "ferocious_bite"        ) a = new        ferocious_bite_t( this );
  else if ( name == "growl"                 ) a = new                 growl_t( this );
  else if ( name == "mangle"                ) a = new                mangle_t( this );
  else if ( name == "mark_of_the_wild"      ) a = new      mark_of_the_wild_t( this );
  //if ( name == "moonfire"              ) return new              moonfire_t( this );
  else if ( name == "prowl"                 ) a = new                 prowl_t( this );
  else if ( name == "regrowth"              ) a = new              regrowth_t( this );
  else if ( name == "shred"                 ) a = new                 shred_t( this );
  //if ( name == "wrath"                 ) return new                 wrath_t( this );

  // Class Talents
  else if ( name == "barkskin"              ) a = new              barkskin_t( this );
  //if ( name == "dash" ||
  //     name == "tiger_dash"            ) return new                  dash_t( this );
  else if ( name == "frenzied_regeneration" ) a = new frenzied_regeneration_t( this );
  else if ( name == "innervate"             ) a = new             innervate_t( this );
  else if ( name == "maim"                  ) a = new                  maim_t( this );
  else if ( name == "moonkin_form"          ) a = new          moonkin_form_t( this );
  else if ( name == "rake"                  ) a = new                  rake_t( this );
  else if ( name == "rejuvenation"          ) a = new          rejuvenation_t( this );
  else if ( name == "rip"                   ) a = new                   rip_t( this );
  //if ( name == "skull_bash"            ) return new            skull_bash_t( this );
  else if ( name == "stampeding_roar"       ) a = new       stampeding_roar_t( this );
  //if ( name == "starfire"              ) return new              starfire_t( this );
  //if ( name == "starsurge"             ) return new     starsurge_t( this, opt );
  else if ( name == "swipe"                 ) a = new           swipe_proxy_t( this );
  else if ( name == "swipe_bear"            ) a = new            swipe_bear_t( this );
  else if ( name == "swipe_cat"             ) a = new             swipe_cat_t( this );
  else if ( name == "wild_growth"           ) a = new           wild_growth_t( this );

  // Multispec Talents
  else if ( name == "berserk" )
  {
    if ( specialization() == DRUID_GUARDIAN )
      a = new berserk_bear_t( this, opt );
    else if ( specialization() == DRUID_FERAL )
      a = new berserk_cat_t( this, opt );
  }

  else if ( name == "survival_instincts"    ) a = new    survival_instincts_t( this, opt );

  // Balance
  else if ( name == "force_of_nature"       ) a = new       force_of_nature_t( this, opt );
  //if ( name == "new_moon"              ) return new              new_moon_t( this, opt );
  //if ( name == "half_moon"             ) return new             half_moon_t( this, opt );
  //if ( name == "full_moon"             ) return new             full_moon_t( this, opt );
  //if ( name == "moons"                 ) return new            moon_proxy_t( this, opt );
  //if ( name == "starfall"              ) return new              starfall_t( this, opt );
 
  // Feral
  else if ( name == "tigers_fury"           ) a = new           tigers_fury_t( this, opt );
  else if ( name == "pounce" ) a = new pounce_t( this, opt );
  else if ( name == "ravage" ) a = new ravage_t( this, opt );

  // Guardian
  else if ( name == "berserk_bear"          ) a = new          berserk_bear_t( this, opt );
  else if ( name == "maul"                  ) a = new                  maul_t( this, opt );
  else if ( name == "enrage" )                a = new enrage_t( this, opt );
  else if ( name == "pulverize"             ) a = new             pulverize_t( this, opt );
  else if ( name == "thrash" )                a = new thrash_t(this, opt);
 
  // Restoration
  else if ( name == "lifebloom"             ) a = new             lifebloom_t( this, opt );
  else if ( name == "nourish"               ) a = new               nourish_t( this, opt );
  else if ( name == "swiftmend"             ) a = new             swiftmend_t( this, opt );
  else if ( name == "tranquility"           ) a = new           tranquility_t( this, opt );
  else if ( name == "tree_of_life" ) a = new tree_of_life_t(this, opt);

  if ( a )
  {
    a->parse_options( opt );

    return a;
  }

  return player_t::create_action( name, opt );
}

// druid_t::init_spells =====================================================
void druid_t::init_spells()
{
  player_t::init_spells();

  // Talents ================================================================

  auto CT  = [ this ]( std::string_view n ) { return find_talent_spell( talent_tree::CLASS, n ); };
  auto ST  = [ this ]( std::string_view n ) { return find_talent_spell( talent_tree::SPECIALIZATION, n ); };
  auto STS = [ this ]( std::string_view n, specialization_e s ) {
    return find_talent_spell( talent_tree::SPECIALIZATION, n, s );
  };

  auto GS = [ this ]( std::string_view n ) { return find_glyph_spell( n ); };

  talent.balance_of_power        = CT( "Balance of Power" );
  talent.berserk                 = CT( "Berserk" );
  talent.blessing_of_the_grove   = CT( "Blessing of the Grove" );
  talent.blood_in_the_water      = CT( "Blood in the Water" );
  talent.brutal_impact           = CT( "Brutal Impact" );
  talent.earth_and_moon          = CT( "Earth and Moon" );
  talent.efflorescence           = CT( "Efflorescence" );
  talent.empowered_touch         = CT( "Empowered Touch" );
  talent.endless_carnage         = CT( "Endless Carnage" );
  talent.euphoria                = CT( "Euphoria" );
  talent.dreamstate              = CT( "Dreamstate" );
  talent.feral_aggression        = CT( "Feral Aggression" );
  talent.feral_charge            = CT( "Feral Charge" );
  talent.feral_swiftness         = CT( "Feral Swiftness" );
  talent.force_of_nature         = CT( "Force of Nature" );
  talent.fungal_growth           = CT( "Fungal Growth" );
  talent.furor                   = CT( "Furor" );
  talent.fury_of_stormrage       = CT( "Fury of Stormrage" );
  talent.fury_swipes             = CT( "Fury Swipes" );
  talent.gale_winds              = CT( "Gale Winds" );
  talent.genesis                 = CT( "Genesis" );
  talent.gift_of_the_earthmother = CT( "Gift of the Earthmother" );
  talent.heart_of_the_wild       = CT( "Heart of the Wild" );
  talent.improved_rejuvenation   = CT( "Improved Rejuvenation" );
  talent.infected_wounds         = CT( "Infected Wounds" );
  talent.king_of_the_jungle      = CT( "King of the Jungle" );
  talent.leader_of_the_pack      = CT( "Leader of the Pack" );
  talent.living_seed             = CT( "Living Seed" );
  talent.lunar_shower            = CT( "Lunar Shower" );
  talent.malfurions_gift         = CT( "Malfurion's Gift" );
  talent.master_shapeshifter     = CT( "Master Shapeshifter" );
  talent.moonglow                = CT( "Moonglow" );
  talent.moonkin_form            = CT( "Moonkin Form" );
  talent.naturalist              = CT( "Naturalist" );
  talent.natural_reaction        = CT( "Natural Reaction" );
  talent.natural_shapeshifter    = CT( "Natural Shapeshifter" );
  talent.natures_bounty          = CT( "Nature's Bounty" );
  talent.natures_cure            = CT( "Nature's Cure" );
  talent.natures_grace           = CT( "Nature's Grace" );
  talent.natures_majesty         = CT( "Nature's Majesty" );
  talent.natures_swiftness       = CT( "Nature's Swiftness" );
  talent.natures_ward            = CT( "Nature's Ward" );
  talent.nurturing_instict       = CT( "Nurturing Instinct" );
  talent.owlkin_frenzy           = CT( "Owlkin Frenzy" );
  talent.predatory_strikes       = CT( "Predatory Strikes" );
  talent.perseverance            = CT( "Perseverance" );
  talent.primal_fury             = CT( "Primal Fury" );
  talent.primal_madness          = CT( "Primal Madness" );
  talent.pulverize               = CT( "Pulverize" );
  talent.rend_and_tear           = CT( "Rend and Tear" );
  talent.revitalize              = CT( "Revitalize" );
  talent.shooting_stars          = CT( "Shooting Stars" );
  talent.solar_beam              = CT( "Solar Beam" );
  talent.stampede                = CT( "Stampede" );
  talent.starfall                = CT( "Starfall" );
  talent.starlight_wrath         = CT( "Starlight Wrath" );
  talent.sunfire                 = CT( "Sunfire" );
  talent.survival_instincts      = CT( "Survival Instincts" );
  talent.swift_rejuvenation      = CT( "Swift Rejuvenation" );
  talent.thick_hide              = CT( "Thick Hide" );
  talent.tree_of_life            = CT( "Tree of Life" );
  talent.typhoon                 = CT( "Typhoon" );
  talent.wild_growth             = CT( "Wild Growth" );

  // Passive Auras
  spec.leather_specialization   = find_specialization_spell( "Leather Specialization" );

  // todo: ap bonus?
  spec.cat_form_passive = find_spell( 3025 );
  spec.bear_form_passive = find_spell( 1178 );

  spec.starsurge = find_specialization_spell("Starsurge");
  spec.moonfury  = find_specialization_spell( "Moonfury" );
  spec.aggression = find_specialization_spell( "Aggression" );
  spec.vengeance  = find_specialization_spell( "Vengeance" );
  spec.mangle     = find_specialization_spell( "Mangle" );
  spec.swiftmend  = find_specialization_spell( "Swiftmend");
  spec.meditation = find_specialization_spell( "Meditation" );
  spec.gift_of_nature = find_specialization_spell( "Gift of Nature" );

  // glyphs
  glyphs.berserk               = GS( "Glyph of Berserk" );
  glyphs.bloodletting          = GS( "Glyph of Bloodletting" );
  glyphs.ferocious_bite        = GS( "Glyph of Ferocious Bite" );
  glyphs.focus                 = GS( "Glyph of Focus" );
  glyphs.frenzied_regeneration = GS( "Glyph of Frenzied Regeneration" );
  glyphs.healing_touch         = GS( "Glyph of Healing Touch" );
  glyphs.innervate             = GS( "Glyph of Innervate" );
  glyphs.insect_swarm          = GS( "Glyph of Insect Swarm" );
  glyphs.lacerate              = GS( "Glyph of Lacerate" );
  glyphs.lifebloom             = GS( "Glyph of Lifebloom" );
  glyphs.mangle                = GS( "Glyph of Mangle" );
  glyphs.mark_of_the_wild      = GS( "Glyph of Mark of the Wild" );
  glyphs.maul                  = GS( "Glyph of Maul" );
  glyphs.monsoon               = GS( "Glyph of Monsoon" );
  glyphs.moonfire              = GS( "Glyph of Moonfire" );
  glyphs.regrowth              = GS( "Glyph of Regrowth" );
  glyphs.rejuvenation          = GS( "Glyph of Rejuvenation" );
  glyphs.rip                   = GS( "Glyph of Rip" );
  glyphs.savage_roar           = GS( "Glyph of Savage Roar" );
  glyphs.starfall              = GS( "Glyph of Starfall" );
  glyphs.starfire              = GS( "Glyph of Starfire" );
  glyphs.starsurge             = GS( "Glyph of Starsurge" );
  glyphs.swiftmend             = GS( "Glyph of Swiftmend" );
  glyphs.tigers_fury           = GS( "Glyph of Tiger's Fury" );
  glyphs.typhoon               = GS( "Glyph of Typhoon" );
  glyphs.wild_growth           = GS( "Glyph of Wild Growth" );
  glyphs.wrath                 = GS( "Glyph of Wrath" );

  // Masteries ==============================================================
  mastery.harmony             = find_mastery_spell( DRUID_RESTORATION );
  mastery.savage_defender     = find_mastery_spell( DRUID_GUARDIAN );
  mastery.razor_claws         = find_mastery_spell( DRUID_FERAL );
  mastery.total_eclipse       = find_mastery_spell( DRUID_BALANCE );
}

// druid_t::init_base =======================================================
void druid_t::init_base_stats()
{
  // Set base distance based on spec
  if ( base.distance < 1 )
    base.distance = 5;

  player_t::init_base_stats();

  base.armor_multiplier *= 1.0 + find_effect( talent.thick_hide, A_MOD_BASE_RESISTANCE_PCT ).percent();
  base.attribute_multiplier[ ATTR_INTELLECT ] *= 1.0 + talent.heart_of_the_wild->effectN( 1 ).percent();

  // Resources
  resources.base[ RESOURCE_RAGE ]         = 100;
  resources.base[ RESOURCE_COMBO_POINT ]  = 5;
  resources.base[ RESOURCE_ENERGY ]       = 100;

  // only activate other resources if you have the affinity and affinity_resources = true
  resources.active_resource[ RESOURCE_HEALTH ]       = true;
  resources.active_resource[ RESOURCE_RAGE ]         = true;
  resources.active_resource[ RESOURCE_MANA ]         = true;
  resources.active_resource[ RESOURCE_COMBO_POINT ]  = true;
  resources.active_resource[ RESOURCE_ENERGY ]       = true;

  // Energy Regen
  resources.base_regen_per_second[ RESOURCE_ENERGY ] = 10;

  base_gcd = 1.5_s;
}

void druid_t::init_finished()
{
  player_t::init_finished();

  // PRECOMBAT WRATH SHENANIGANS
  // we do this here so all precombat actions have gone throught init() and init_finished() so if-expr are properly
  // parsed and we can adjust wrath travel times accordingly based on subsequent precombat actions that will sucessfully
  // cast
  for ( auto pre = precombat_action_list.begin(); pre != precombat_action_list.end(); pre++ )
  {
    /* if ( auto wr = dynamic_cast<spells::wrath_t*>( *pre ) )
    {
      std::for_each( pre + 1, precombat_action_list.end(), [ wr ]( action_t* a ) {
        // unnecessary offspec resources are disabled by default, so evaluate any if-expr on the candidate action first
        // so we don't call action_ready() on possible offspec actions that will require off-spec resources to be
        // enabled
        if ( a->harmful && ( !a->if_expr || a->if_expr->success() ) && a->action_ready() )
          wr->harmful = false;  // more harmful actions exist, set current wrath to non-harmful so we can keep casting

        if ( a->name_str == wr->name_str )
          wr->count++;  // see how many wrath casts are left, so we can adjust travel time when combat begins
      } );

      // if wrath is still harmful then it is the final precast spell, so we set the energize type to NONE, which will
      // then be accounted for in wrath_t::execute()
      if ( wr->harmful )
        wr->energize_type = action_energize::NONE;
    }*/
  }
}

// druid_t::init_buffs ======================================================
void druid_t::create_buffs()
{
  player_t::create_buffs();

  using namespace buffs;

  // Baseline
  buff.barkskin = make_buff( this, "barkskin", find_class_spell( "Barkskin" ) )
    ->set_cooldown( 0_ms )
    ->set_default_value_from_effect_type( A_MOD_DAMAGE_PERCENT_TAKEN )
    ->set_refresh_behavior( buff_refresh_behavior::DURATION )
    ->set_tick_behavior( buff_tick_behavior::NONE );

  buff.bear_form = make_buff<bear_form_buff_t>( this );

  buff.cat_form = make_buff<cat_form_buff_t>( this );

  buff.dash = make_buff( this, "dash", find_class_spell( "Dash" ) )
    ->set_cooldown( 0_ms )
    ->set_default_value_from_effect_type( A_MOD_INCREASE_SPEED );

  buff.prowl = make_buff( this, "prowl", find_class_spell( "Prowl" ) );

  // Class
  buff.innervate = make_buff(this, "innervate", find_class_spell("Innervate") );

  buff.moonkin_form = make_buff_fallback<moonkin_form_buff_t>( talent.moonkin_form.ok(), this, "moonkin_form" );

  // Multi-spec
  // The buff ID in-game is same as the talent, 61336, but the buff effects (as well as tooltip reference) is in 50322
  buff.survival_instincts =
      make_buff_fallback( talent.survival_instincts.ok(), this, "survival_instincts", talent.survival_instincts )
          ->set_cooldown( 0_ms )
          ->set_default_value( find_effect( find_spell( 50322 ), A_MOD_DAMAGE_PERCENT_TAKEN ).percent() );

  // Balance buffs

  /* buff.eclipse_lunar = make_buff_fallback( talent.eclipse.ok(), this, "eclipse_lunar", spec.eclipse_lunar )
    ->set_default_value_from_effect_type( A_ADD_PCT_MODIFIER, P_GENERIC )
    ->set_stack_change_callback( [ this ]( buff_t*, int old_, int new_ ) {
      if ( old_ && !new_ )
      {
        eclipse_handler.advance_eclipse();
      }
    } );

  buff.eclipse_solar = make_buff_fallback( talent.eclipse.ok(), this, "eclipse_solar", spec.eclipse_solar )
    ->set_default_value_from_effect_type( A_ADD_PCT_MODIFIER, P_GENERIC )
    ->set_stack_change_callback( [ this ]( buff_t*, int old_, int new_ ) {
      if ( old_ && !new_ )
      {
        eclipse_handler.advance_eclipse();
      }
    } );*/

  // trigger this somehow, it also has icd
  buff.natures_grace = make_buff_fallback( talent.natures_grace.ok(), this, "natures_grace", find_spell( 16886 ) )
                           ->set_default_value_from_effect_type( A_MOD_CASTING_SPEED_NOT_STACK )
    ->set_pct_buff_type( STAT_PCT_BUFF_HASTE );

  buff.owlkin_frenzy = make_buff_fallback( talent.owlkin_frenzy.ok(), this, "owlkin_frenzy", find_class_spell( "Owlkin Frenzy" ) )
          ->set_chance( find_effect( find_specialization_spell( "Owlkin Frenzy" ), A_ADD_FLAT_MODIFIER, P_PROC_CHANCE )
                            .percent() );

  /* buff.starfall = make_buff_fallback( talent.starfall.ok(), this, "starfall",
                                           find_spell( 191034 ) )  // lookup via spellid for convoke
          ->set_stack_behavior( buff_stack_behavior::ASYNCHRONOUS )
          ->set_freeze_stacks( true )
          ->set_partial_tick( true )  // TODO: confirm true?
          ->set_tick_behavior( buff_tick_behavior::REFRESH );  // TODO: confirm true?*/

  // Feral buffs
  /* buff.berserk_cat =
      make_buff_fallback<berserk_cat_buff_t>( talent.berserk.ok(),
      this, "berserk_cat", spec.berserk_cat );*/

  // 1.05s ICD per https://github.com/simulationcraft/simc/commit/b06d0685895adecc94e294f4e3fcdd57ac909a10
  buff.clearcasting = make_buff( this, "clearcasting", find_class_spell( "Omen of Clarity" ))
          ->set_cooldown( 1.05_s );

  buff.fury_swipes =
      make_buff_fallback( talent.fury_swipes.ok(), this, "fury_swipes", find_class_spell( "Fury Swipes" ) )
          ->set_cooldown( 3_s );

  buff.predatory_swiftness =
      make_buff_fallback( talent.predatory_strikes.ok(), this, "predatory_swiftness", find_spell( 69369 ) );

  buff.tigers_fury = make_buff( this, "tigers_fury", find_class_spell( "Tiger's Fury" ) )
                         ->set_cooldown( 0_ms )
                         ->set_default_value_from_effect( 1 );

  buff.stampede_cat = make_buff_fallback( talent.stampede.ok(), this, "stampede_cat",
                                          find_spell( talent.stampede.rank() > 1 ? 81022 : 81021 ) )
                          ->set_default_value_from_effect( 2 );

  buff.stampede_bear = make_buff_fallback( talent.stampede.ok(), this, "stampede_bear",
                                           find_spell( talent.stampede.rank() > 1 ? 81017 : 81016 ) );
  buff.enrage        = make_buff( this, "enrage", find_class_spell( "Enrage" ) );

  //buff.pulverize = make_buff_fallback( talent.pulverize.ok(), this "pulverize", find_spell("")


  // Restoration buffs
  // todo: malfurions gift trigger
  /* buff.clearcasting_tree = make_buff_fallback( talent.omen_of_clarity_tree.ok(),
      this, "clearcasting_tree", find_trigger( talent.omen_of_clarity_tree ).trigger() )
          ->set_chance( find_trigger( talent.omen_of_clarity_tree ).percent() )
          ->set_name_reporting( "clearcasting" );*/

  buff.natures_swiftness =
      make_buff_fallback( talent.natures_swiftness.ok(), this, "natures_swiftness", talent.natures_swiftness )
          ->set_cooldown( 0_ms )
          ->set_stack_change_callback( [ this ]( buff_t*, int, int new_ ) {
            if ( !new_ )
              cooldown.natures_swiftness->start();
          } );

  buff.harmony = 
      make_buff_fallback( specialization() == DRUID_RESTORATION, this, "harmony", find_spell( 100977 ) );

  // todo: tree duration
  buff.tree_of_life = make_buff_fallback( talent.tree_of_life.ok(), this, "tree_of_life", find_spell( 5420 ) )
                          ->set_duration( find_spell( 33891 )->duration() )
                          ->add_invalidate(CACHE_PLAYER_HEAL_MULTIPLIER);
}

// Create active actions ====================================================
void druid_t::create_actions()
{
  using namespace cat_attacks;
  using namespace bear_attacks;
  using namespace spells;
  using namespace heals;
  using namespace auto_attacks;

  // Melee Attacks
  if ( !caster_melee_attack )
    caster_melee_attack = new caster_melee_t( this );

  if ( !cat_melee_attack )
  {
    init_beast_weapon( cat_weapon, 1.0 );
    cat_melee_attack = new cat_melee_t( this );
  }

  if ( !bear_melee_attack )
  {
    init_beast_weapon( bear_weapon, 2.5 );
    bear_melee_attack = new bear_melee_t( this );
  }

  // General
  active.shift_to_caster = get_secondary_action<cancel_form_t>( "cancel_form_shift", "" );
  active.shift_to_caster->dual = true;
  active.shift_to_caster->background = true;

  active.shift_to_bear = get_secondary_action<bear_form_t>( "bear_form_shift", "" );
  active.shift_to_bear->dual = true;

  active.shift_to_cat = get_secondary_action<cat_form_t>( "cat_form_shift", "" );
  active.shift_to_cat->dual = true;

  player_t::create_actions();
}

// Default Consumables ======================================================
std::string druid_t::default_flask() const
{
  return "disabled";
}

std::string druid_t::default_potion() const
{
  return "disabled";
}

std::string druid_t::default_food() const
{
  return "disabled";
}

std::string druid_t::default_rune() const
{
  return "disabled";
}

std::string druid_t::default_temporary_enchant() const
{
  return "disabled";
}

// ALL Spec Pre-Combat Action Priority List =================================
void druid_t::apl_precombat()
{
  action_priority_list_t* precombat = get_action_priority_list( "precombat" );

  // Consumables
  precombat->add_action( "flask" );
  precombat->add_action( "food" );
  precombat->add_action( "augmentation" );
  precombat->add_action( "snapshot_stats", "Snapshot raid buffed stats before combat begins and pre-potting is done." );
}

// NO Spec Combat Action Priority List ======================================
void druid_t::apl_default()
{
  action_priority_list_t* def = get_action_priority_list( "default" );

  // Assemble Racials / On-Use Items / Professions
  for ( const auto& action_str : get_racial_actions() )
    def->add_action( action_str );

  for ( const auto& action_str : get_item_actions() )
    def->add_action( action_str );

  for ( const auto& action_str : get_profession_actions() )
    def->add_action( action_str );
}

// Action Priority Lists ========================================
void druid_t::apl_feral()
{
#include "class_modules/apl/feral_apl.inc"
}

void druid_t::apl_balance()
{
#include "class_modules/apl/balance_apl.inc"
}

void druid_t::apl_balance_ptr()
{
#include "class_modules/apl/balance_apl_ptr.inc"
}

void druid_t::apl_guardian()
{
#include "class_modules/apl/guardian_apl.inc"
}

void druid_t::apl_restoration()
{
#include "class_modules/apl/restoration_druid_apl.inc"
}

// druid_t::init_scaling ====================================================
void druid_t::init_scaling()
{
  player_t::init_scaling();

  scaling->disable( STAT_STRENGTH );

  // workaround for resto dps scaling
  if ( specialization() == DRUID_RESTORATION )
  {
    scaling->disable( STAT_AGILITY );
    scaling->disable( STAT_MASTERY_RATING );
    scaling->disable( STAT_WEAPON_DPS );
    scaling->enable( STAT_INTELLECT );
  }

  // Save a copy of the weapon
  caster_form_weapon = main_hand_weapon;

  // Bear/Cat form weapons need to be scaled up if we are calculating scale factors for the weapon
  // dps. The actual cached cat/bear form weapons are created before init_scaling is called, so the
  // adjusted values for the "main hand weapon" have not yet been added.
  if ( sim->scaling->scale_stat == STAT_WEAPON_DPS )
  {
    if ( cat_weapon.damage > 0 )
    {
      auto coeff = sim->scaling->scale_value * cat_weapon.swing_time.total_seconds();
      cat_weapon.damage += coeff;
      cat_weapon.min_dmg += coeff;
      cat_weapon.max_dmg += coeff;
      cat_weapon.dps += sim->scaling->scale_value;
    }

    if ( bear_weapon.damage > 0 )
    {
      auto coeff = sim->scaling->scale_value * bear_weapon.swing_time.total_seconds();
      bear_weapon.damage += coeff;
      bear_weapon.min_dmg += coeff;
      bear_weapon.max_dmg += coeff;
      bear_weapon.dps += sim->scaling->scale_value;
    }
  }
}

// druid_t::init_gains ======================================================
void druid_t::init_gains()
{
  player_t::init_gains();
    gain.natures_balance     = get_gain( "Natures Balance" );
    gain.stellar_innervation = get_gain( "Stellar Innervation" );

    gain.energy_refund       = get_gain( "Energy Refund" );
    gain.primal_fury         = get_gain( "Primal Fury" );
    gain.tigers_tenacity     = get_gain( "Tiger's Tenacity" );

    gain.bear_form           = get_gain( "Bear Form" );
    gain.rage_from_melees    = get_gain( "Rage from Melees" );
    gain.primal_madness      = get_gain( "Primal Madness" );
    gain.enrage              = get_gain( "Enrage" );
  
    // Multi-spec
    gain.clearcasting = get_gain( "Clearcasting" );  // Feral & Restoration
}

// druid_t::init_procs ======================================================
void druid_t::init_procs()
{
  player_t::init_procs();

  // Feral
  proc.primal_fury          = get_proc( "Primal Fury" );
  proc.clearcasting         = get_proc( "Clearcasting" );
  proc.clearcasting_wasted  = get_proc( "Clearcasting (Wasted)" );
  proc.fury_swipes          = get_proc( "Fury Swipes" );

 }

// druid_t::init_uptimes ====================================================
void druid_t::init_uptimes()
{
  player_t::init_uptimes();
}

// druid_t::init_resources ==================================================
void druid_t::init_resources( bool force )
{
  player_t::init_resources( force );

  resources.current[ RESOURCE_RAGE ]         = 0;
  resources.current[ RESOURCE_COMBO_POINT ]  = 0;
}

// druid_t::init_rng ========================================================
void druid_t::init_rng()
{
  player_t::init_rng();
}

// druid_t::init_special_effects ============================================
void druid_t::init_special_effects()
{
  struct druid_cb_t : public dbc_proc_callback_t
  {
    druid_cb_t( druid_t* p, const special_effect_t& e ) : dbc_proc_callback_t( p, e ) {}

    druid_t* p() { return static_cast<druid_t*>( listener ); }
  };

  // General
  if ( unique_gear::find_special_effect( this, 388069 ) )
  {
    callbacks.register_callback_execute_function( 388069,
      []( const dbc_proc_callback_t* cb, action_t* a, action_state_t* s ) {
        if ( a->special )
        {
          switch ( a->data().id() )
          {
            case 190984:  // wrath
            case 394111:  // sundered firmament
            case 191034:  // starfall
            case 78674:   // starsurge
            case 274281:  // new moon
            case 274282:  // half moon
            case 274283:  // full moon
              break;
            default:
              return;
          }
        }
        cb->proc_action->execute_on_target( s->target );
      } );
  }

  // blanket proc callback initialization happens here. anything further needs to be individually initialized
  player_t::init_special_effects();
}

// druid_t::init_actions ====================================================
void druid_t::init_action_list()
{
  if ( !action_list_str.empty() )
  {
    player_t::init_action_list();
    return;
  }

  clear_action_priority_lists();

  apl_precombat();  // PRE-COMBAT

  switch ( specialization() )
  {
    case DRUID_FERAL:       apl_feral();                                  break;
    case DRUID_BALANCE:     is_ptr() ? apl_balance_ptr() : apl_balance(); break;
    case DRUID_GUARDIAN:    apl_guardian();                               break;
    case DRUID_RESTORATION: apl_restoration();                            break;
    default:                apl_default();                                break;
  }

  use_default_action_list = true;

  player_t::init_action_list();
}

// druid_t::reset ===========================================================
void druid_t::reset()
{
  player_t::reset();

  // Reset druid_t variables to their original state.
  form = NO_FORM;
  base_gcd = 1.5_s;

  // Restore main hand attack / weapon to normal state
  main_hand_attack = caster_melee_attack;
  main_hand_weapon = caster_form_weapon;

  // Reset runtime variables
  dot_list.moonfire.clear();
  dot_list.sunfire.clear();
}

// druid_t::merge ===========================================================
void druid_t::merge( player_t& other )
{
  player_t::merge( other );

  druid_t& od = static_cast<druid_t&>( other );

  for ( size_t i = 0; i < counters.size(); i++ )
    counters[ i ]->merge( *od.counters[ i ] );

  //eclipse_handler.merge( od.eclipse_handler );
}

void druid_t::datacollection_begin()
{
  player_t::datacollection_begin();

  //eclipse_handler.datacollection_begin();
}

void druid_t::datacollection_end()
{
  player_t::datacollection_end();

  //eclipse_handler.datacollection_end();
}

// druid_t::mana_regen_per_second ===========================================
double druid_t::resource_regen_per_second( resource_e r ) const
{
  double reg = player_t::resource_regen_per_second( r );

  if ( r == RESOURCE_MANA )
  {
    if ( specialization() == DRUID_BALANCE && buff.moonkin_form->check() )
      reg *= ( 1.0 + buff.moonkin_form->data().effectN( 5 ).percent() ) / cache.spell_haste();
  }

  return reg;
}

// druid_t::available =======================================================
timespan_t druid_t::available() const
{
  if ( primary_resource() != RESOURCE_ENERGY )
    return player_t::available();

  double energy = resources.current[ RESOURCE_ENERGY ];

  if ( energy > 25 )
    return 100_ms;

  return std::max( timespan_t::from_seconds( ( 25 - energy ) / resource_regen_per_second( RESOURCE_ENERGY ) ), 100_ms );
}

// druid_t::invalidate_cache ================================================
void druid_t::invalidate_cache( cache_e c )
{
  player_t::invalidate_cache( c );

  switch ( c )
  {
    case CACHE_ATTACK_POWER:
      if ( specialization() == DRUID_GUARDIAN || specialization() == DRUID_FERAL )
        invalidate_cache( CACHE_SPELL_POWER );
      break;
    case CACHE_SPELL_POWER:
      if ( specialization() == DRUID_BALANCE || specialization() == DRUID_RESTORATION )
        invalidate_cache( CACHE_ATTACK_POWER );
      break;
    case CACHE_CRIT_CHANCE:
      if ( specialization() == DRUID_GUARDIAN )
        invalidate_cache( CACHE_DODGE );
      break;
    default: break;
  }
}

// Composite combat stat override functions =================================



// Attack Power =============================================================
double druid_t::composite_attack_power_multiplier() const
{
  double ap = player_t::composite_attack_power_multiplier();

  if ( buff.cat_form->check() )
    ap *= 1.0 + talent.heart_of_the_wild->effectN(2).percent();

  if ( specialization() == DRUID_FERAL )
    ap *= 1.0 + spec.aggression->effectN(1).percent();

  return ap;
}

// Armor ====================================================================

double druid_t::composite_armor_multiplier() const
{
  double a = player_t::composite_armor_multiplier();

  if ( buff.bear_form->check() )
  {
    a *= 1.0 + buff.bear_form->data().effectN( 4 ).percent();
    a *= 1.0 + talent.thick_hide->effectN( 2 ).percent();
  }

  return a;
}

// Critical Strike ==========================================================
double druid_t::composite_melee_crit_chance() const
{
  double mcc = player_t::composite_melee_crit_chance();
  if ( buff.cat_form->check() )
    mcc += talent.master_shapeshifter->effectN( 1 ).percent();
  return mcc;
}

double druid_t::composite_spell_crit_chance() const
{
  double scc = player_t::composite_spell_crit_chance();
  if ( buff.cat_form->check() )
    scc += talent.master_shapeshifter->effectN( 1 ).percent();
  return scc;
}

// Defense ==================================================================
double druid_t::composite_crit_avoidance() const
{
  double c = player_t::composite_crit_avoidance();

  if ( buff.bear_form->check() )
    c *= 1.0 + talent.thick_hide->effectN( 3 ).percent();

  return c;
}

double druid_t::composite_dodge_rating() const
{
  double dr = player_t::composite_dodge_rating();

  // Theres no effect listed for this??
  if (buff.bear_form->check() || buff.cat_form->check())
    dr += talent.feral_swiftness.rank() * 2;
  if ( buff.bear_form->check() )
    dr += talent.natural_reaction->effectN(1).percent();
  return dr;
}

// Miscellaneous ============================================================
double druid_t::composite_attribute_multiplier( attribute_e attr ) const
{
  double m = player_t::composite_attribute_multiplier( attr );

  switch ( attr )
  {
    case ATTR_STAMINA:
      if ( buff.bear_form->check() )
      {
        m *= 1.0 + spec.bear_form_passive->effectN( 2 ).percent();
        m *= 1.0 + talent.heart_of_the_wild->effectN( 1 ).percent();
      }
      break;
    default: break;
  }

  return m;
}

double druid_t::matching_gear_multiplier( attribute_e attr ) const
{
  unsigned idx;

  switch ( attr )
  {
    case ATTR_AGILITY: idx = 1; break;
    case ATTR_INTELLECT: idx = 2; break;
    case ATTR_STAMINA: idx = 3; break;
    default: return 0;
  }

  return spec.leather_specialization->effectN( idx ).percent();
}

double druid_t::composite_melee_expertise( const weapon_t* ) const
{
  double exp = player_t::composite_melee_expertise();

  if ( buff.bear_form->check() )
    exp += buff.bear_form->data().effectN( 6 ).base_value();

  return exp;
}

// Movement =================================================================
double druid_t::temporary_movement_modifier() const
{
  double active = player_t::temporary_movement_modifier();

  if ( buff.dash->up() && buff.cat_form->check() )
    active = std::max( active, buff.dash->check_value() );

  return active;
}

double druid_t::passive_movement_modifier() const
{
  double ms = player_t::passive_movement_modifier();

  if ( buff.cat_form->check() )
    ms += talent.feral_swiftness->effectN( 1 ).percent();

  return ms;
}

// Expressions ==============================================================
std::unique_ptr<expr_t> druid_t::create_action_expression( action_t& a, std::string_view name_str )
{
  auto splits = util::string_split<std::string_view>( name_str, "." );

  if ( splits[ 0 ] == "ticks_gained_on_refresh" ||
       ( splits.size() > 2 && ( splits[ 0 ] == "druid" || splits[ 0 ] == "dot" ) &&
         splits[ 2 ] == "ticks_gained_on_refresh" ) )
  {
    bool pmul = false;
    if ( ( splits.size() > 1 && splits[ 1 ] == "pmult" ) || ( splits.size() > 4 && splits[ 3 ] == "pmult" ) )
      pmul = true;

    action_t* dot_action = nullptr;

    if ( splits.size() > 2 )
    {
      if ( splits[ 1 ] == "rake" )
        dot_action = find_action( "rake_bleed" );
      else
        dot_action = find_action( splits[ 1 ] );

      if ( !dot_action )
        throw std::invalid_argument( "invalid action specified in ticks_gained_on_refresh" );
    }
    else
      dot_action = &a;

    action_t* source_action = &a;
    double multiplier = 1.0;

    return make_fn_expr( name_str, [ dot_action, source_action, multiplier, pmul ]() -> double {
      auto ticks_gained_func = []( double mod, action_t* dot_action, player_t* target, bool pmul ) -> double {
        action_state_t* state = dot_action->get_state();
        state->target = target;
        dot_action->snapshot_state( state, result_amount_type::DMG_OVER_TIME );

        dot_t* dot = dot_action->get_dot( target );
        timespan_t ttd = target->time_to_percent( 0 );
        timespan_t duration = dot_action->composite_dot_duration( state ) * mod;

        double remaining_ticks = std::min( dot->remains(), ttd ) / dot_action->tick_time( state ) *
                                 ( ( pmul && dot->state ) ? dot->state->persistent_multiplier : 1.0 );
        double new_ticks = std::min( dot_action->calculate_dot_refresh_duration( dot, duration ), ttd ) /
                           dot_action->tick_time( state ) * ( pmul ? state->persistent_multiplier : 1.0 );

        action_state_t::release( state );
        return new_ticks - remaining_ticks;
      };

      if ( source_action->aoe == -1 )
      {
        double accum = 0.0;
        for ( player_t* target : source_action->targets_in_range_list( source_action->target_list() ) )
          accum += ticks_gained_func( multiplier, dot_action, target, pmul );

        return accum;
      }

      return ticks_gained_func( multiplier, dot_action, source_action->target, pmul );
    } );
  }

  return player_t::create_action_expression( a, name_str );
}

void druid_t::create_options()
{
  player_t::create_options();

  // General
  add_option( opt_bool( "druid.no_cds", options.no_cds ) );
  add_option( opt_bool( "druid.raid_combat", options.raid_combat ) );

  // Balance
  add_option( opt_int( "druid.initial_moon_stage", options.initial_moon_stage ) );
 
  // Restoration
  add_option( opt_float( "druid.time_spend_healing", options.time_spend_healing ) );
}

role_e druid_t::primary_role() const
{
  // First, check for the user-specified role
  switch ( player_t::primary_role() )
  {
    case ROLE_TANK:
    case ROLE_ATTACK:
    case ROLE_SPELL:
      return player_t::primary_role();
      break;
    default:
      break;
  }

  // Else, fall back to spec
  switch ( specialization() )
  {
    case DRUID_BALANCE:
      return ROLE_SPELL; break;
    case DRUID_GUARDIAN:
      return ROLE_TANK; break;
    default:
      return ROLE_ATTACK;
      break;
  }
}

stat_e druid_t::convert_hybrid_stat( stat_e s ) const
{
  // this converts hybrid stats that either morph based on spec or only work for certain specs into the appropriate
  // "basic" stats
  switch ( s )
  {
    case STAT_STR_AGI_INT:
      switch ( specialization() )
      {
        case DRUID_BALANCE:
        case DRUID_RESTORATION: return STAT_INTELLECT;
        case DRUID_FERAL:
        case DRUID_GUARDIAN: return STAT_AGILITY;
        default: return STAT_NONE;
      }
    case STAT_AGI_INT:
      if ( specialization() == DRUID_BALANCE || specialization() == DRUID_RESTORATION )
        return STAT_INTELLECT;
      else
        return STAT_AGILITY;
    case STAT_STR_AGI: return STAT_AGILITY;
    case STAT_STR_INT: return STAT_INTELLECT;
    case STAT_SPIRIT:
      if ( specialization() == DRUID_RESTORATION )
        return s;
      else
        return STAT_NONE;
    case STAT_BONUS_ARMOR:
      if ( specialization() == DRUID_GUARDIAN )
        return s;
      else
        return STAT_NONE;
    default: return s;
  }
}

resource_e druid_t::primary_resource() const
{
  if ( specialization() == DRUID_BALANCE )
    return RESOURCE_ASTRAL_POWER;

  if ( specialization() == DRUID_GUARDIAN )
    return RESOURCE_RAGE;

  if ( primary_role() == ROLE_HEAL || primary_role() == ROLE_SPELL )
    return RESOURCE_MANA;

  return RESOURCE_ENERGY;
}

void druid_t::init_absorb_priority()
{
  player_t::init_absorb_priority();
}

void druid_t::target_mitigation( school_e school, result_amount_type type, action_state_t* s )
{
  s->result_amount *= 1.0 + buff.barkskin->value();

  s->result_amount *= 1.0 + buff.survival_instincts->value();

  s->result_amount *= 1.0 + talent.thick_hide->effectN( 1 ).percent();

  player_t::target_mitigation( school, type, s );
}

// Trigger effects based on being hit or taking damage.
void druid_t::assess_damage_imminent_pre_absorb( school_e school, result_amount_type dmg, action_state_t* s )
{
  player_t::assess_damage_imminent_pre_absorb( school, dmg, s );

  if ( action_t::result_is_hit( s->result ) && s->result_amount > 0 )
  {
    // Guardian rage from melees
    /* if ( specialization() == DRUID_GUARDIAN && !s->action->special && cooldown.rage_from_melees->up() )
    {
      resource_gain( RESOURCE_RAGE, spec.bear_form_passive->effectN( 3 ).base_value(), gain.rage_from_melees );
      cooldown.rage_from_melees->start( cooldown.rage_from_melees->duration );
    }*/

    if ( buff.moonkin_form->check() && s->result_type == result_amount_type::DMG_DIRECT )
      buff.owlkin_frenzy->trigger();
  }
}

void druid_t::shapeshift( form_e f )
{
  if ( get_form() == f )
    return;

  buff.stampede_bear->expire();
  buff.enrage->expire();

  buff.bear_form->expire();
  buff.cat_form->expire();
  buff.moonkin_form->expire();

  switch ( f )
  {
    case BEAR_FORM:    buff.bear_form->trigger();    break;
    case CAT_FORM:     buff.cat_form->trigger();     break;
    case MOONKIN_FORM: buff.moonkin_form->trigger(); break;
    case NO_FORM:                                    break;
    default: assert( 0 ); break;
  }

  form = f;
}

// Target Data ==============================================================
druid_td_t::druid_td_t( player_t& target, druid_t& source )
  : actor_target_data_t( &target, &source ), dots(), hots(), buff()
{
  if ( target.is_enemy() )
  {
    dots.moonfire              = target.get_dot( "moonfire", &source );
    dots.rake                  = target.get_dot( "rake", &source );
    dots.rip                   = target.get_dot( "rip", &source );
    dots.sunfire               = target.get_dot( "sunfire", &source );
  }
  else
  {
    hots.frenzied_regeneration = target.get_dot( "frenzied_regeneration", &source );
    hots.lifebloom             = target.get_dot( "lifebloom", &source );
    hots.regrowth              = target.get_dot( "regrowth", &source );
    hots.rejuvenation          = target.get_dot( "rejuvenation", &source );
    hots.wild_growth           = target.get_dot( "wild_growth", &source );
  }

}

bool druid_td_t::hots_ticking() const
{
  return hots.rejuvenation->is_ticking() + hots.regrowth->is_ticking() + hots.lifebloom->is_ticking();
}

const druid_td_t* druid_t::find_target_data( const player_t* target ) const
{
  assert( target );
  return target_data[ target ];
}

druid_td_t* druid_t::get_target_data( player_t* target ) const
{
  assert( target );
  druid_td_t*& td = target_data[ target ];
  if ( !td )
    td = new druid_td_t( *target, const_cast<druid_t&>( *this ) );

  return td;
}

void druid_t::init_beast_weapon( weapon_t& w, double swing_time )
{
  // use main hand weapon as base
  w = main_hand_weapon;

  if ( w.type == WEAPON_NONE )
  {
    // if main hand weapon is empty, use unarmed damage unarmed base beast weapon damage range is 1-1 Jul 25 2018
    w.min_dmg = w.max_dmg = w.damage = 1;
  }
  else
  {
    // Otherwise normalize the main hand weapon's damage to the beast weapon's speed.
    double normalizing_factor = swing_time / w.swing_time.total_seconds();
    w.min_dmg *= normalizing_factor;
    w.max_dmg *= normalizing_factor;
    w.damage *= normalizing_factor;
  }

  w.type       = WEAPON_BEAST;
  w.school     = SCHOOL_PHYSICAL;
  w.swing_time = timespan_t::from_seconds( swing_time );
}

void druid_t::moving()
{
  if ( ( executing && !executing->usable_moving() ) || ( channeling && !channeling->usable_moving() ) )
    player_t::interrupt();
}

const spell_data_t* druid_t::apply_override( const spell_data_t* base, const spell_data_t* passive )
{
  if ( !passive->ok() )
    return base;

  return find_spell( as<unsigned>( find_effect( passive, base, A_OVERRIDE_ACTION_SPELL ).base_value() ) );
}

void druid_t::copy_from( player_t* source )
{
  player_t::copy_from( source );

  options = static_cast<druid_t*>( source )->options;
}

void druid_t::apply_affecting_auras( action_t& action )
{
  player_t::apply_affecting_auras( action );

  // Spec-wide auras
  action.apply_affecting_aura( spec_spell );

  action.apply_affecting_aura( talent.natural_shapeshifter );
  action.apply_affecting_aura( talent.genesis );
}

/* Report Extension Class
 * Here you can define class specific report extensions/overrides
 */
class druid_report_t : public player_report_extension_t
{
private:
  struct feral_counter_data_t
  {
    action_t* action = nullptr;
    double tf_exe = 0.0;
    double tf_tick = 0.0;
    double bt_exe = 0.0;
    double bt_tick = 0.0;
  };

public:
  druid_report_t( druid_t& player ) : p( player ) {}

  void html_customsection( report::sc_html_stream& os ) override
  {
    if ( p.specialization() == DRUID_FERAL )
    {
      os << "<div class=\"player-section custom_section\">\n";

      feral_snapshot_table( os );

      os << "</div>\n";
    }

    /* if ( p.specialization() == DRUID_BALANCE && p.eclipse_handler.enabled() )
    {
      os << "<div class=\"player-section custom_section\">\n";

      balance_eclipse_table( os );

      os << "</div>\n";
    }*/
  }

  void balance_print_data( report::sc_html_stream& os, const spell_data_t* spell )
  {
    /* double iter = data[ eclipse_state_e::MAX_STATE ];
    double none  = data[ eclipse_state_e::ANY_NEXT ];
    double solar = data[ eclipse_state_e::IN_SOLAR ];
    double lunar = data[ eclipse_state_e::IN_LUNAR ];
    double both  = data[ eclipse_state_e::IN_BOTH ];
    double total = none + solar + lunar + both;

    if ( !total )
      return;

    os.format( "<tr><td class=\"left\">{}</td>"
               "<td class=\"right\">{:.2f}</td><td class=\"right\">{:.1f}%</td>"
               "<td class=\"right\">{:.2f}</td><td class=\"right\">{:.1f}%</td>"
               "<td class=\"right\">{:.2f}</td><td class=\"right\">{:.1f}%</td>"
               "<td class=\"right\">{:.2f}</td><td class=\"right\">{:.1f}%</td></tr>",
               report_decorators::decorated_spell_data( p.sim, spell ),
               none / iter, none / total * 100,
               solar / iter, solar / total * 100,
               lunar / iter, lunar / total * 100,
               both / iter, both / total * 100 );*/
  }

  void balance_eclipse_table( report::sc_html_stream& os )
  {
    os << "<h3 class=\"toggle open\">Eclipse Utilization</h3>\n"
       << "<div class=\"toggle-content\">\n"
       << "<table class=\"sc even\">\n"
       << "<thead><tr><th></th>\n"
       << "<th colspan=\"2\">None</th><th colspan=\"2\">Solar</th><th colspan=\"2\">Lunar</th><th colspan=\"2\">Both</th>\n"
       << "</tr></thead>\n";

    /* balance_print_data( os, p.find_class_spell( "Wrath" ), *p.eclipse_handler.data.wrath );
    //balance_print_data( os, p.talent.starfire, *p.eclipse_handler.data.starfire );
    //balance_print_data( os, p.talent.starsurge, *p.eclipse_handler.data.starsurge );
    //balance_print_data( os, p.talent.starfall, *p.eclipse_handler.data.starfall );
    if ( p.eclipse_handler.data.fury_of_elune )
      balance_print_data( os, p.find_spell( 202770 ), *p.eclipse_handler.data.fury_of_elune );
    if ( p.eclipse_handler.data.new_moon )
      balance_print_data( os, p.find_spell( 274281 ), *p.eclipse_handler.data.new_moon );
    if ( p.eclipse_handler.data.half_moon )
      balance_print_data( os, p.find_spell( 274282 ), *p.eclipse_handler.data.half_moon );
    if ( p.eclipse_handler.data.full_moon )
      balance_print_data( os, p.find_spell( 274283 ), *p.eclipse_handler.data.full_moon );
      */
    os << "</table>\n"
       << "</div>\n";
  }

  void feral_parse_counter( snapshot_counter_t* counter, feral_counter_data_t& data )
  {
    if ( range::contains( counter->buffs, p.buff.tigers_fury ) )
    {
      data.tf_exe += counter->execute.mean();

      if ( counter->tick.count() )
        data.tf_tick += counter->tick.mean();
      else
        data.tf_tick += counter->execute.mean();
    }
  }

  void feral_snapshot_table( report::sc_html_stream& os )
  {
    // Write header
    os << "<h3 class=\"toggle open\">Snapshot Table</h3>\n"
       << "<div class=\"toggle-content\">\n"
       << "<table class=\"sc sort even\">\n"
       << "<thead><tr><th></th>\n"
       << "<th colspan=\"2\">Tiger's Fury</th>\n";

    os << "</tr>\n";

    os << "<tr>\n"
       << "<th class=\"toggle-sort\" data-sortdir=\"asc\" data-sorttype=\"alpha\">Ability Name</th>\n"
       << "<th class=\"toggle-sort\">Execute %</th>\n"
       << "<th class=\"toggle-sort\">Benefit %</th>\n";

    os << "</tr></thead>\n";

    std::vector<feral_counter_data_t> data_list;

    // Compile and Write Contents
    for ( size_t i = 0; i < p.counters.size(); i++ )
    {
      auto counter = p.counters[ i ].get();
      feral_counter_data_t data;

      for ( const auto& a : counter->stats->action_list )
      {
        if ( a->s_data->ok() )
        {
          data.action = a;
          break;
        }
      }

      if ( !data.action )
        data.action = counter->stats->action_list.front();

      // We can change the action's reporting name here since we shouldn't need to access it again later in the report
      std::string suf = "_convoke";
      if ( suf.size() <= data.action->name_str.size() &&
           std::equal( suf.rbegin(), suf.rend(), data.action->name_str.rbegin() ) )
      {
        data.action->name_str_reporting += "Convoke";
      }

      feral_parse_counter( counter, data );

      // since the BT counter is created immediately following the TF counter for the same stat in
      // cat_attacks_t::init(), check the next counter and add in the data if it's for the same stat
      if ( i + 1 < p.counters.size() )
      {
        auto next_counter = p.counters[ i + 1 ].get();

        if ( counter->stats == next_counter->stats )
        {
          feral_parse_counter( next_counter, data );
          i++;
        }
      }

      if ( data.tf_exe + data.tf_tick + data.bt_exe + data.bt_tick == 0.0 )
        continue;

      data_list.push_back( std::move( data ) );
    }

    range::sort( data_list, []( const feral_counter_data_t& l, const feral_counter_data_t& r ) {
      return l.action->name_str < r.action->name_str;
    } );

    for ( const auto& data : data_list )
    {
      os.format( R"(<tr><td class="left">{}</td><td class="right">{:.2f}%</td><td class="right">{:.2f}%</td>)",
                 report_decorators::decorated_action( *data.action ), data.tf_exe * 100, data.tf_tick * 100 );

      os << "</tr>\n";
    }

    // Write footer
    os << "</table>\n"
       << "</div>\n";
  }

private:
  druid_t& p;
};

// DRUID MODULE INTERFACE ===================================================
struct druid_module_t : public module_t
{
  druid_module_t() : module_t( DRUID ) {}

  player_t* create_player( sim_t* sim, std::string_view name, race_e r = RACE_NONE ) const override
  {
    auto p = new druid_t( sim, name, r );
    p->report_extension = std::make_unique<druid_report_t>( *p );
    return p;
  }
  bool valid() const override { return true; }

  void init( player_t* p ) const override
  {
  }

  void static_init() const override {}

  void register_hotfixes() const override
  {
  }

  void combat_begin( sim_t* ) const override {}
  void combat_end( sim_t* ) const override {}
};
}  // UNNAMED NAMESPACE

const module_t* module_t::druid()
{
  static druid_module_t m;
  return &m;
}
