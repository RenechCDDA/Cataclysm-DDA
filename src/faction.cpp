#include "faction.h"

#include <algorithm>
#include <bitset>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "avatar.h"
#include "basecamp.h"
#include "calendar.h"
#include "catacharset.h"
#include "character.h"
#include "coordinates.h"
#include "cursesdef.h"
#include "debug.h"
#include "display.h"
#include "faction_camp.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "input_context.h"
#include "line.h"
#include "localized_comparator.h"
#include "map.h"
#include "map_scale_constants.h"
#include "memory_fast.h"
#include "mission_companion.h"
#include "mtype.h"
#include "npc.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "point.h"
#include "skill.h"
#include "string_formatter.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "uilist.h"
#include "ui_manager.h"
#include "vitamin.h"

static const faction_id faction_no_faction( "no_faction" );
static const faction_id faction_your_followers( "your_followers" );

static const flag_id json_flag_TWO_WAY_RADIO( "TWO_WAY_RADIO" );

namespace npc_factions
{
static std::vector<faction_template> all_templates;
} // namespace npc_factions

faction_template::faction_template()
{
    likes_u = 0;
    respects_u = 0;
    trusts_u = 0;
    known_by_u = true;
    food_supply.calories = 0;
    wealth = 0;
    size = 0;
    power = 0;
    lone_wolf_faction = false;
    limited_area_claim = false;
    currency = itype_id::NULL_ID();
}

faction::faction( const faction_template &templ )
{
    id = templ.id;
    // first init *all* members, than copy those from the template
    static_cast<faction_template &>( *this ) = templ;
}

void faction_template::load( const JsonObject &jsobj )
{
    faction_template fac( jsobj );
    npc_factions::all_templates.emplace_back( fac );
}

void faction_template::check_consistency()
{
    for( const faction_template &fac : npc_factions::all_templates ) {
        for( const faction_epilogue_data &epi : fac.epilogue_data ) {
            if( !epi.epilogue.is_valid() ) {
                debugmsg( "There's no snippet with id %s", epi.epilogue.str() );
            }
        }
    }
}

void faction_template::reset()
{
    npc_factions::all_templates.clear();
}

void faction_template::load_relations( const JsonObject &jsobj )
{
    for( const JsonMember fac : jsobj.get_object( "relations" ) ) {
        JsonObject rel_jo = fac.get_object();
        std::bitset<static_cast<size_t>( npc_factions::relationship::rel_types )> fac_relation( 0 );
        for( const auto &rel_flag : npc_factions::relation_strs ) {
            fac_relation.set( static_cast<size_t>( rel_flag.second ),
                              rel_jo.get_bool( rel_flag.first, false ) );
        }
        relations[fac.name()] = fac_relation;
    }
}
faction_price_rule faction_price_rules_reader::get_next( JsonValue &jv )
{
    JsonObject jo = jv.get_object();
    faction_price_rule ret( icg_entry_reader::_part_get_next( jo ) );
    optional( jo, false, "markup", ret.markup, 1.0 );
    optional( jo, false, "premium", ret.premium, 1.0 );
    optional( jo, false, "fixed_adj", ret.fixed_adj, std::nullopt );
    optional( jo, false, "price", ret.price, std::nullopt );
    return ret;
}

faction_template::faction_template( const JsonObject &jsobj )
    : name( jsobj.get_string( "name" ) )
    , likes_u( jsobj.get_int( "likes_u" ) )
    , respects_u( jsobj.get_int( "respects_u" ) )
    , trusts_u( jsobj.get_int( "trusts_u", 0 ) )
    , known_by_u( jsobj.get_bool( "known_by_u" ) )
    , id( faction_id( jsobj.get_string( "id" ) ) )
    , size( jsobj.get_int( "size" ) )
    , power( jsobj.get_int( "power" ) )
    , food_supply()
    , wealth( jsobj.get_int( "wealth" ) )
{
    jsobj.get_member( "description" ).read( desc );
    optional( jsobj, false, "consumes_food", consumes_food, false );
    optional( jsobj, false, "price_rules", price_rules, faction_price_rules_reader {} );
    jsobj.read( "fac_food_supply", food_supply, true );
    if( jsobj.has_string( "currency" ) ) {
        jsobj.read( "currency", currency, true );
        price_rules.emplace_back( currency, 1, 0 );
    } else {
        currency = itype_id::NULL_ID();
    }
    lone_wolf_faction = jsobj.get_bool( "lone_wolf_faction", false );
    limited_area_claim = jsobj.get_bool( "limited_area_claim", false );
    load_relations( jsobj );
    mon_faction = mfaction_str_id( jsobj.get_string( "mon_faction", "human" ) );
    optional( jsobj, false, "epilogues", epilogue_data );
}

std::string faction::describe() const
{
    std::string ret = desc.translated();
    return ret;
}

void faction_power_spec::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "faction", faction );
    optional( jo, false, "power_min", power_min );
    optional( jo, false, "power_max", power_max );

    if( !power_min.has_value() && !power_max.has_value() ) {
        jo.throw_error( "Must have either a power_min or a power_max" );
    }
}

void faction_epilogue_data::deserialize( const JsonObject &jo )
{
    optional( jo, false, "power_min", power_min );
    optional( jo, false, "power_max", power_max );
    optional( jo, false, "dynamic", dynamic_conditions );
    mandatory( jo, false, "id", epilogue );
}


bool faction::check_relations( const std::vector<faction_power_spec> &faction_power_specs ) const
{
    if( faction_power_specs.empty() ) {
        return true;
    }
    for( const faction_power_spec &spec : faction_power_specs ) {
        if( spec.power_min.has_value() ) {
            if( spec.faction->power < spec.power_min.value() ) {
                return false;
            }
        }
        if( spec.power_max.has_value() ) {
            if( spec.faction->power >= spec.power_max.value() ) {
                return false;
            }
        }
    }
    return true;
}


std::vector<std::string> faction::epilogue() const
{
    std::vector<std::string> ret;
    for( const faction_epilogue_data &epi : epilogue_data ) {
        if( ( !epi.power_min.has_value() || power >= epi.power_min ) && ( !epi.power_max.has_value() ||
                power < epi.power_max ) ) {
            if( check_relations( epi.dynamic_conditions ) ) {
                ret.emplace_back( epi.epilogue->translated() );
            }
        }
    }
    return ret;
}

void faction::add_to_membership( const character_id &guy_id, const std::string &guy_name,
                                 const bool known )
{
    members[guy_id] = std::make_pair( guy_name, known );
}

void faction::remove_member( const character_id &guy_id )
{
    for( auto it = members.cbegin(), next_it = it; it != members.cend(); it = next_it ) {
        ++next_it;
        if( guy_id == it->first ) {
            members.erase( it );
            break;
        }
    }
    if( members.empty() ) {
        for( const faction_template &elem : npc_factions::all_templates ) {
            // This is a templated base faction - don't delete it, just leave it as zero members for now.
            // Only want to delete dynamically created factions.
            if( elem.id == id ) {
                return;
            }
        }
        g->faction_manager_ptr->remove_faction( id );
    }
}

// Used in game.cpp
std::string fac_ranking_text( int val )
{
    if( val <= -100 ) {
        return _( "Archenemy" );
    }
    if( val <= -80 ) {
        return _( "Wanted Dead" );
    }
    if( val <= -60 ) {
        return _( "Enemy of the People" );
    }
    if( val <= -40 ) {
        return _( "Wanted Criminal" );
    }
    if( val <= -20 ) {
        return _( "Not Welcome" );
    }
    if( val <= -10 ) {
        return _( "Pariah" );
    }
    if( val <= -5 ) {
        return _( "Disliked" );
    }
    if( val >= 100 ) {
        return _( "Hero" );
    }
    if( val >= 80 ) {
        return _( "Idol" );
    }
    if( val >= 60 ) {
        return _( "Beloved" );
    }
    if( val >= 40 ) {
        return _( "Highly Valued" );
    }
    if( val >= 20 ) {
        return _( "Valued" );
    }
    if( val >= 10 ) {
        return _( "Well-Liked" );
    }
    if( val >= 5 ) {
        return _( "Liked" );
    }

    return _( "Neutral" );
}

// Used in game.cpp
std::string fac_respect_text( int val )
{
    // Respected, feared, etc.
    if( val >= 100 ) {
        return pgettext( "Faction respect", "Legendary" );
    }
    if( val >= 80 ) {
        return pgettext( "Faction respect", "Unchallenged" );
    }
    if( val >= 60 ) {
        return pgettext( "Faction respect", "Mighty" );
    }
    if( val >= 40 ) {
        return pgettext( "Faction respect", "Famous" );
    }
    if( val >= 20 ) {
        return pgettext( "Faction respect", "Well-Known" );
    }
    if( val >= 10 ) {
        return pgettext( "Faction respect", "Spoken Of" );
    }

    // Disrespected, laughed at, etc.
    if( val <= -100 ) {
        return pgettext( "Faction respect", "Worthless Scum" );
    }
    if( val <= -80 ) {
        return pgettext( "Faction respect", "Vermin" );
    }
    if( val <= -60 ) {
        return pgettext( "Faction respect", "Despicable" );
    }
    if( val <= -40 ) {
        return pgettext( "Faction respect", "Parasite" );
    }
    if( val <= -20 ) {
        return pgettext( "Faction respect", "Leech" );
    }
    if( val <= -10 ) {
        return pgettext( "Faction respect", "Laughingstock" );
    }

    return pgettext( "Faction respect", "Neutral" );
}

std::string fac_wealth_text( int val, int size )
{
    //Wealth per person
    val = val / size;
    if( val >= 1000000 ) {
        return pgettext( "Faction wealth", "Filthy rich" );
    }
    if( val >= 750000 ) {
        return pgettext( "Faction wealth", "Affluent" );
    }
    if( val >= 500000 ) {
        return pgettext( "Faction wealth", "Prosperous" );
    }
    if( val >= 250000 ) {
        return pgettext( "Faction wealth", "Well-Off" );
    }
    if( val >= 100000 ) {
        return pgettext( "Faction wealth", "Comfortable" );
    }
    if( val >= 85000 ) {
        return pgettext( "Faction wealth", "Wanting" );
    }
    if( val >= 70000 ) {
        return pgettext( "Faction wealth", "Failing" );
    }
    if( val >= 50000 ) {
        return pgettext( "Faction wealth", "Impoverished" );
    }
    return pgettext( "Faction wealth", "Destitute" );
}

std::string faction::food_supply_text()
{
    //Convert to how many days you can support the population
    int val = food_supply.kcal() / ( size * 288 );
    if( val >= 30 ) {
        return pgettext( "Faction food", "Overflowing" );
    }
    if( val >= 14 ) {
        return pgettext( "Faction food", "Well-Stocked" );
    }
    if( val >= 6 ) {
        return pgettext( "Faction food", "Scrapping By" );
    }
    if( val >= 3 ) {
        return pgettext( "Faction food", "Malnourished" );
    }
    return pgettext( "Faction food", "Starving" );
}

nc_color faction::food_supply_color()
{
    int val = food_supply.kcal() / ( size * 288 );
    if( val >= 30 ) {
        return c_green;
    } else if( val >= 14 ) {
        return c_light_green;
    } else if( val >= 6 ) {
        return c_yellow;
    } else if( val >= 3 ) {
        return c_light_red;
    } else {
        return c_red;
    }
}

std::pair<nc_color, std::string> faction::vitamin_stores( vitamin_type vit_type )
{
    bool is_toxin = vit_type == vitamin_type::TOXIN;
    const double days_of_food = food_supply.kcal() / 3000.0;
    std::map<vitamin_id, int> stored_vits = food_supply.vitamins();
    // First, pare down our search to only the relevant type
    for( auto it = stored_vits.cbegin(); it != stored_vits.cend(); ) {
        if( it->first->type() != vit_type ) {
            it = stored_vits.erase( it );
        } else {
            ++it;
        }
    }
    if( stored_vits.empty() ) {
        return std::pair<nc_color, std::string>( !is_toxin ? c_red : c_green, _( "None present (NONE)" ) );
    }
    std::vector<std::pair<vitamin_id, double>> vitamins;
    // Iterate the map's content into a sortable container...
    for( auto &vit : stored_vits ) {
        int units_per_day = vit.first.obj().units_absorption_per_day();
        double relative_intake = static_cast<double>( vit.second ) / static_cast<double>
                                 ( units_per_day ) / days_of_food;
        // We use the inverse value for toxins, since they are bad.
        if( is_toxin ) {
            relative_intake = 1 / relative_intake;
        }
        vitamins.emplace_back( vit.first, relative_intake );
    }
    // Sort to find the worst-case scenario, lowest relative_intake is first
    std::sort( vitamins.begin(), vitamins.end(), []( const auto & x, const auto & y ) {
        return x.second > y.second;
    } );
    const double worst_intake = vitamins.at( 0 ).second;
    std::string vit_name = vitamins.at( 0 ).first.obj().name();
    std::string msg = is_toxin ? _( "(TRACE)" ) : _( "(PLENTY)" );
    if( worst_intake <= 0.3 ) {
        msg = is_toxin ? _( "(POISON)" ) : _( "(LACK)" );
        return std::pair<nc_color, std::string>( c_red, string_format( _( "%1$s %2$s" ), vit_name,
                msg ) );
    }
    if( worst_intake <= 1.0 ) {
        msg = is_toxin ? _( "(DANGER)" ) : _( "(MEAGER)" );
        return std::pair<nc_color, std::string>( c_yellow, string_format( _( "%1$s %2$s" ), vit_name,
                msg ) );
    }
    return std::pair<nc_color, std::string>( c_green, string_format( _( "%1$s %2$s" ), vit_name,
            msg ) );
}

faction_price_rule const *faction::get_price_rules( item const &it, npc const &guy ) const
{
    auto const el = std::find_if(
    price_rules.crbegin(), price_rules.crend(), [&it, &guy]( faction_price_rule const & fc ) {
        return fc.matches( it, guy );
    } );
    if( el != price_rules.crend() ) {
        return &*el;
    }
    return nullptr;
}

bool faction::has_relationship( const faction_id &guy_id, npc_factions::relationship flag ) const
{
    for( const auto &rel_data : relations ) {
        if( rel_data.first == guy_id.c_str() ) {
            return rel_data.second.test( static_cast<size_t>( flag ) );
        }
    }
    return false;
}

std::string fac_combat_ability_text( int val )
{
    if( val >= 150 ) {
        return pgettext( "Faction combat lvl", "Legendary" );
    }
    if( val >= 130 ) {
        return pgettext( "Faction combat lvl", "Expert" );
    }
    if( val >= 115 ) {
        return pgettext( "Faction combat lvl", "Veteran" );
    }
    if( val >= 105 ) {
        return pgettext( "Faction combat lvl", "Skilled" );
    }
    if( val >= 95 ) {
        return pgettext( "Faction combat lvl", "Competent" );
    }
    if( val >= 85 ) {
        return pgettext( "Faction combat lvl", "Untrained" );
    }
    if( val >= 75 ) {
        return pgettext( "Faction combat lvl", "Crippled" );
    }
    if( val >= 50 ) {
        return pgettext( "Faction combat lvl", "Feeble" );
    }
    return pgettext( "Faction combat lvl", "Worthless" );
}

void npc_factions::finalize()
{
    g->faction_manager_ptr->create_if_needed();
}

void faction_manager::clear()
{
    factions.clear();
}

void faction_manager::remove_faction( const faction_id &id )
{
    if( id.str().empty() || id == faction_no_faction ) {
        return;
    }
    for( auto it = factions.cbegin(), next_it = it; it != factions.cend(); it = next_it ) {
        ++next_it;
        if( id == it->first ) {
            factions.erase( it );
            break;
        }
    }
}

void faction_manager::create_if_needed()
{
    if( !factions.empty() ) {
        return;
    }
    for( const faction_template &fac_temp : npc_factions::all_templates ) {
        factions[fac_temp.id] = faction( fac_temp );
    }
}

faction *faction_manager::add_new_faction( const std::string &name_new, const faction_id &id_new,
        const faction_id &template_id )
{
    for( const faction_template &fac_temp : npc_factions::all_templates ) {
        if( template_id == fac_temp.id ) {
            faction fac( fac_temp );
            fac.name = name_new;
            fac.id = id_new;
            factions[fac.id] = fac;
            return &factions[fac.id];
        }
    }
    return nullptr;
}

faction *faction_manager::get( const faction_id &id, const bool complain )
{
    if( id.is_null() ) {
        return get( faction_no_faction );
    }
    for( auto &elem : factions ) {
        if( elem.first == id ) {
            if( !elem.second.validated ) {
                for( const faction_template &fac_temp : npc_factions::all_templates ) {
                    if( fac_temp.id == id ) {
                        elem.second.currency = fac_temp.currency;
                        elem.second.price_rules = fac_temp.price_rules;
                        elem.second.lone_wolf_faction = fac_temp.lone_wolf_faction;
                        elem.second.limited_area_claim = fac_temp.limited_area_claim;
                        elem.second.name = fac_temp.name;
                        elem.second.desc = fac_temp.desc;
                        elem.second.mon_faction = fac_temp.mon_faction;
                        elem.second.epilogue_data = fac_temp.epilogue_data;
                        for( const auto &rel_data : fac_temp.relations ) {
                            if( elem.second.relations.find( rel_data.first ) == elem.second.relations.end() ) {
                                elem.second.relations[rel_data.first] = rel_data.second;
                            }
                        }
                        break;
                    }
                }
                elem.second.validated = true;
            }
            return &elem.second;
        }
    }
    for( const faction_template &elem : npc_factions::all_templates ) {
        // id isn't already in factions map, so load in the template.
        if( elem.id == id ) {
            factions[elem.id] = faction( elem );
            if( !factions.empty() ) {
                factions[elem.id].validated = true;
            }
            return &factions[elem.id];
        }
    }
    // Sometimes we add new IDs to the map, sometimes we want to check if its already there.
    if( complain ) {
        debugmsg( "Requested non-existing faction '%s'", id.str() );
    }
    return nullptr;
}

template<>
bool string_id<faction>::is_valid() const
{
    return g->faction_manager_ptr->get( *this, false ) != nullptr;
}

void basecamp::faction_display( const catacurses::window &fac_w, const int width ) const
{
    int y = 2;
    const nc_color col = c_white;
    Character &player_character = get_player_character();
    const tripoint_abs_omt player_abspos = player_character.pos_abs_omt();
    tripoint_abs_omt camp_pos = camp_omt_pos();
    std::string direction = direction_name( direction_from( player_abspos, camp_pos ) );
    mvwprintz( fac_w, point( width, ++y ), c_light_gray, _( "Press enter to rename this camp" ) );
    if( direction != "center" ) {
        mvwprintz( fac_w, point( width, ++y ), c_light_gray, _( "Direction: to the " ) + direction );
    }
    mvwprintz( fac_w, point( width, ++y ), col, _( "Location: %s" ), camp_pos.to_string() );
    faction *yours = player_character.get_faction();
    std::string food_text = string_format( _( "Food Supply: %s %d kilocalories" ),
                                           yours->food_supply_text(), yours->food_supply.kcal() );
    nc_color food_col = yours->food_supply_color();
    mvwprintz( fac_w, point( width, ++y ), food_col, food_text );
    std::pair<nc_color, std::string> vitamins = yours->vitamin_stores( vitamin_type::VITAMIN );
    mvwprintz( fac_w, point( width, ++y ), vitamins.first, _( "Worst vitamin:" ) + vitamins.second );
    std::pair<nc_color, std::string> toxins = yours->vitamin_stores( vitamin_type::TOXIN );
    mvwprintz( fac_w, point( width, ++y ), toxins.first, _( "Worst toxin:" ) + toxins.second );
    std::string bldg = next_upgrade( base_camps::base_dir, 1 );
    std::string bldg_full = _( "Next Upgrade: " ) + bldg;
    mvwprintz( fac_w, point( width, ++y ), col, bldg_full );
}

void faction::faction_display( const catacurses::window &fac_w, const int width ) const
{
    int y = 2;
    mvwprintz( fac_w, point( width, ++y ), c_light_gray, _( "Attitude to you:           %s" ),
               fac_ranking_text( likes_u ) );
    fold_and_print( fac_w, point( width, ++y ), getmaxx( fac_w ) - width - 2, c_light_gray,
                    "%s", desc );
}

std::string npc::get_current_status() const
{
    if( current_target() != nullptr ) {
        return _( "In Combat!" );
    } else if( in_sleep_state() ) {
        return _( "Sleeping" );
    } else if( is_following() ) {
        return _( "Following" );
    } else if( is_leader() ) {
        return _( "Leading" );
    } else if( is_patrolling() ) {
        return _( "Patrolling" );
    } else if( is_guarding() ) {
        return _( "Guarding" );
    } else {
        return get_current_activity();
    }
}

int npc::faction_display( const catacurses::window &fac_w, const int width ) const
{
    const map &here = get_map();

    int retval = 0;
    int y = 2;
    const nc_color col = c_white;
    Character &player_character = get_player_character();
    const tripoint_abs_omt player_abspos = player_character.pos_abs_omt();

    //get NPC followers, status, direction, location, needs, weapon, etc.
    mvwprintz( fac_w, point( width, ++y ), c_light_gray, _( "Press enter to talk to this follower " ) );
    std::string mission_string;
    if( has_companion_mission() ) {
        std::string dest_string;
        std::optional<tripoint_abs_omt> dest = get_mission_destination();
        if( dest ) {
            basecamp *dest_camp;
            std::optional<basecamp *> temp_camp = overmap_buffer.find_camp( dest->xy() );
            if( temp_camp ) {
                dest_camp = *temp_camp;
                dest_string = _( "traveling to: " ) + dest_camp->camp_name();
            } else {
                dest_string = string_format( _( "traveling to: %s" ), dest->to_string() );
            }
            mission_string = _( "Current Mission: " ) + dest_string;
        } else {
            npc_companion_mission c_mission = get_companion_mission();
            mission_string = _( "Current Mission: " ) + action_of( c_mission.miss_id.id );
        }
        fold_and_print( fac_w, point( width, ++y ), getmaxx( fac_w ) - width - 2, col, mission_string );

        // Determine remaining time in mission, and display it
        std::string mission_eta;
        if( companion_mission_time_ret < calendar::turn ) {
            mission_eta = _( "JOB COMPLETED" );
        } else {
            mission_eta = _( "ETA: " ) + to_string( companion_mission_time_ret - calendar::turn );
        }
        fold_and_print( fac_w, point( width, ++y ), getmaxx( fac_w ) - width - 2, col, mission_eta );
    }

    tripoint_abs_omt guy_abspos = pos_abs_omt();
    basecamp *temp_camp = nullptr;
    if( assigned_camp ) {
        std::optional<basecamp *> bcp = overmap_buffer.find_camp( ( *assigned_camp ).xy() );
        if( bcp ) {
            temp_camp = *bcp;
        }
    }
    const bool is_stationed = assigned_camp && temp_camp;
    std::string direction = direction_name( direction_from( player_abspos, guy_abspos ) );
    if( direction != "center" ) {
        mvwprintz( fac_w, point( width, ++y ), col, _( "Direction: to the " ) + direction );
    } else {
        mvwprintz( fac_w, point( width, ++y ), col, _( "Direction: Nearby" ) );
    }
    if( is_stationed ) {
        mvwprintz( fac_w, point( width, ++y ), col, _( "Location: %s, at camp: %s" ),
                   guy_abspos.to_string(), temp_camp->camp_name() );
    } else {
        mvwprintz( fac_w, point( width, ++y ), col, _( "Location: %s" ), guy_abspos.to_string() );
    }
    std::string can_see;
    nc_color see_color;

    bool u_has_radio = player_character.cache_has_item_with_flag( json_flag_TWO_WAY_RADIO, true );
    bool guy_has_radio = cache_has_item_with_flag( json_flag_TWO_WAY_RADIO, true );
    // is the NPC even in the same area as the player?
    if( rl_dist( player_abspos, pos_abs_omt() ) > 3 ||
        ( rl_dist( player_character.pos_abs(), pos_abs() ) > SEEX * 2 ||
          !player_character.sees( here, pos_bub( here ) ) ) ) {
        if( u_has_radio && guy_has_radio ) {
            if( !( player_character.posz() >= 0 && posz() >= 0 ) &&
                !( player_character.posz() == posz() ) ) {
                //Early exit
                can_see = _( "Not within radio range" );
                see_color = c_light_red;
            } else {
                // TODO: better range calculation than just elevation.
                const int base_range = 200;
                float send_elev_boost = ( 1 + ( player_character.posz() * 0.1 ) );
                float recv_elev_boost = ( 1 + ( posz() * 0.1 ) );
                if( ( square_dist( player_character.pos_abs_sm(),
                                   pos_abs_sm() ) <= base_range * send_elev_boost * recv_elev_boost ) ) {
                    //Direct radio contact, both of their elevation are in effect
                    retval = 2;
                    can_see = _( "Within radio range" );
                    see_color = c_light_green;
                } else {
                    //contact via camp radio tower
                    int recv_range = base_range * recv_elev_boost;
                    int send_range = base_range * send_elev_boost;
                    const int radio_tower_boost = 5;
                    // find camps that are near player or npc
                    const std::vector<camp_reference> &camps_near_player = overmap_buffer.get_camps_near(
                                player_character.pos_abs_sm(), send_range * radio_tower_boost );
                    const std::vector<camp_reference> &camps_near_npc = overmap_buffer.get_camps_near(
                                pos_abs_sm(), recv_range * radio_tower_boost );
                    bool camp_to_npc = false;
                    bool camp_to_camp = false;
                    for( const camp_reference &i : camps_near_player ) {
                        if( !i.camp->has_provides( "radio" ) ) {
                            continue;
                        }
                        if( camp_to_camp ||
                            square_dist( i.abs_sm_pos, pos_abs_sm() ) <= recv_range * radio_tower_boost ) {
                            //one radio tower relay
                            camp_to_npc = true;
                            break;
                        }
                        for( const camp_reference &j : camps_near_npc ) {
                            //two radio tower relays
                            if( ( j.camp )->has_provides( "radio" ) &&
                                ( square_dist( i.abs_sm_pos, j.abs_sm_pos ) <= base_range * radio_tower_boost *
                                  radio_tower_boost ) ) {
                                camp_to_camp = true;
                                break;
                            }
                        }
                    }
                    if( camp_to_npc || camp_to_camp ) {
                        retval = 2;
                        can_see = _( "Within radio range" );
                        see_color = c_light_green;
                    } else {
                        can_see = _( "Not within radio range" );
                        see_color = c_light_red;
                    }
                }
            }
        } else if( guy_has_radio && !u_has_radio ) {
            can_see = _( "You do not have a radio" );
            see_color = c_light_red;
        } else if( !guy_has_radio && u_has_radio ) {
            can_see = _( "Follower does not have a radio" );
            see_color = c_light_red;
        } else {
            can_see = _( "Both you and follower need a radio" );
            see_color = c_light_red;
        }
    } else {
        retval = 1;
        can_see = _( "Within interaction range" );
        see_color = c_light_green;
    }
    // TODO: NPCS on mission contactable same as traveling
    if( has_companion_mission() ) {
        can_see = _( "Press enter to recall from their mission." );
        see_color = c_light_red;
    }
    mvwprintz( fac_w, point( width, ++y ), see_color, "%s", can_see );
    nc_color status_col = col;
    if( current_target() != nullptr ) {
        status_col = c_light_red;
    }
    mvwprintz( fac_w, point( width, ++y ), status_col, _( "Status: " ) + get_current_status() );
    if( is_stationed && has_job() ) {
        mvwprintz( fac_w, point( width, ++y ), col, _( "Working at camp" ) );
    } else if( is_stationed ) {
        mvwprintz( fac_w, point( width, ++y ), col, _( "Idling at camp" ) );
    }

    const std::pair <std::string, nc_color> condition = hp_description();
    mvwprintz( fac_w, point( width, ++y ), condition.second, _( "Condition: " ) + condition.first );
    const std::pair <std::string, nc_color> hunger_pair = display::hunger_text_color( *this );
    const std::pair <std::string, nc_color> thirst_pair = display::thirst_text_color( *this );
    const std::pair <std::string, nc_color> sleepiness_pair = display::sleepiness_text_color( *this );
    const std::string nominal = pgettext( "needs", "Nominal" );
    mvwprintz( fac_w, point( width, ++y ), hunger_pair.second,
               _( "Hunger: " ) + ( hunger_pair.first.empty() ? nominal : hunger_pair.first ) );
    mvwprintz( fac_w, point( width, ++y ), thirst_pair.second,
               _( "Thirst: " ) + ( thirst_pair.first.empty() ? nominal : thirst_pair.first ) );
    mvwprintz( fac_w, point( width, ++y ), sleepiness_pair.second,
               _( "Sleepiness: " ) + ( sleepiness_pair.first.empty() ? nominal : sleepiness_pair.first ) );
    int lines = fold_and_print( fac_w, point( width, ++y ), getmaxx( fac_w ) - width - 2, c_white,
                                _( "Wielding: " ) + weapname_simple() );
    y += lines;

    const auto skillslist = Skill::get_skills_sorted_by( [&]( const Skill & a, const Skill & b ) {
        const int level_a = get_skill_level( a.ident() );
        const int level_b = get_skill_level( b.ident() );
        return localized_compare( std::make_pair( -level_a, a.name() ),
                                  std::make_pair( -level_b, b.name() ) );
    } );
    size_t count = 0;
    std::vector<std::string> skill_strs;
    for( size_t i = 0; i < skillslist.size() && count < 3; i++ ) {
        if( !skillslist[ i ]->is_combat_skill() ) {
            std::string skill_str = string_format( "%s: %d", skillslist[i]->name(),
                                                   static_cast<int>( get_skill_level( skillslist[i]->ident() ) ) );
            skill_strs.push_back( skill_str );
            count += 1;
        }
    }
    std::string best_three_noncombat = _( "Best other skills: " );
    std::string best_skill_text = string_format( _( "Best combat skill: %s: %d" ),
                                  best_combat_skill( combat_skills::NO_GENERAL ).first.obj().name(),
                                  best_combat_skill( combat_skills::NO_GENERAL ).second );
    mvwprintz( fac_w, point( width, ++y ), col, best_skill_text );
    mvwprintz( fac_w, point( width, ++y ), col, best_three_noncombat + skill_strs[0] );
    mvwprintz( fac_w, point( width + utf8_width( best_three_noncombat ), ++y ), col, skill_strs[1] );
    mvwprintz( fac_w, point( width + utf8_width( best_three_noncombat ), ++y ), col, skill_strs[2] );
    return retval;
}

void faction_manager::display() const
{
    input_context ctxt;
    faction_manager_impl p_impl;

    ctxt.register_navigate_ui_list();
    ctxt.register_leftright();
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    // Smooths out our handling, makes tabs load immediately after input instead of waiting for next.
    ctxt.set_timeout( 10 );

    while( true ) {
        ui_manager::redraw_invalidated();


        if( !p_impl.get_is_open() ) {
            break;
        }
    }
}

void faction_manager_impl::draw_controls()
{
    ImGui::SetWindowSize( ImVec2( window_width, window_height ), ImGuiCond_Once );
    ImGui::Text( "Hello, world!" );
    // random stuff to make sure controls work
    if( ImGui::Button( "Show me an error" ) ) {
        debugmsg( "Congratulations this is an error! Prepare to crash, and make it double!" );
    }
    if( ImGui::Button( "Close me!" ) ) {
        is_open = false;
    }
}
