#include "npctalk_rules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cata_imgui.h"
#include "dialogue.h"
#include "game.h"
#include "npctalk.h"
#include "ui_manager.h"
#include "imgui/imgui.h"

static std::map<cbm_recharge_rule, std::string> recharge_map = {
    {cbm_recharge_rule::CBM_RECHARGE_ALL, "<ally_rule_cbm_recharge_all_text>" },
    {cbm_recharge_rule::CBM_RECHARGE_MOST, "<ally_rule_cbm_recharge_most_text>" },
    {cbm_recharge_rule::CBM_RECHARGE_SOME, "<ally_rule_cbm_recharge_some_text>" },
    {cbm_recharge_rule::CBM_RECHARGE_LITTLE, "<ally_rule_cbm_recharge_little_text>" },
    {cbm_recharge_rule::CBM_RECHARGE_NONE, "<ally_rule_cbm_recharge_none_text>" },
};

static std::map<cbm_reserve_rule, std::string> reserve_map = {
    {cbm_reserve_rule::CBM_RESERVE_ALL, "<ally_rule_cbm_reserve_all_text>" },
    {cbm_reserve_rule::CBM_RESERVE_MOST, "<ally_rule_cbm_reserve_most_text>" },
    {cbm_reserve_rule::CBM_RESERVE_SOME, "<ally_rule_cbm_reserve_some_text>" },
    {cbm_reserve_rule::CBM_RESERVE_LITTLE, "<ally_rule_cbm_reserve_little_text>" },
    {cbm_reserve_rule::CBM_RESERVE_NONE, "<ally_rule_cbm_reserve_none_text>" },
};

static std::map<combat_engagement, std::string> engagement_rules = {
    {combat_engagement::NONE, "<ally_rule_engagement_none>" },
    {combat_engagement::CLOSE, "<ally_rule_engagement_close>" },
    {combat_engagement::WEAK, "<ally_rule_engagement_weak>" },
    {combat_engagement::HIT, "<ally_rule_engagement_hit>" },
    {combat_engagement::ALL, "<ally_rule_engagement_all>" },
    {combat_engagement::FREE_FIRE, "<ally_rule_engagement_free_fire>" },
    {combat_engagement::NO_MOVE, "<ally_rule_engagement_no_move>" },
};

static std::map<aim_rule, std::string> aim_rule_map = {
    {aim_rule::WHEN_CONVENIENT, "<ally_rule_aim_when_convenient>" },
    {aim_rule::SPRAY, "<ally_rule_aim_spray>" },
    {aim_rule::PRECISE, "<ally_rule_aim_precise>" },
    {aim_rule::STRICTLY_PRECISE, "<ally_rule_aim_strictly_precise>" },
};

void follower_rules_ui::draw_follower_rules_ui( npc *guy )
{
    input_context ctxt;
    follower_rules_ui_impl p_impl;
    p_impl.set_npc_pointer_to( guy );
    p_impl.input_ptr = &ctxt;

    ctxt.register_navigate_ui_list();
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "CONFIRM", to_translation( "Set, toggle, or reset selected rule" ) );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ANY_INPUT" );
    // This is still bizarrely necessary for imgui
    ctxt.set_timeout( 10 );

    while( true ) {
        ui_manager::redraw_invalidated();


        p_impl.last_action = ctxt.handle_input();

        if( p_impl.last_action == "QUIT" || !p_impl.get_is_open() ) {
            break;
        }
    }
}

template<typename T>
void follower_rules_ui_impl::multi_rule_header( std::string id, T &rule,
        std::map<T, std::string> rule_map, bool should_advance )
{
    if( ImGui::InvisibleButton( id.c_str(), ImVec2() ) || should_advance ) {
        if( rule_map.upper_bound( rule ) == rule_map.end() ) {
            // Then we have the *last* entry the map, so wrap to the first element
            rule = rule_map.begin()->first;
        } else {
            // Assign the next possible value contained in the map.
            rule = rule_map.upper_bound( rule )->first;
        }
        int offset = distance( rule_map.begin(), rule_map.find( rule ) );
        ImGui::SetKeyboardFocusHere( offset );
    }
}

void follower_rules_ui_impl::set_npc_pointer_to( npc *new_guy )
{
    guy = new_guy;
}

std::string follower_rules_ui_impl::get_parsed( std::string initial_string )
{
    std::string string_reference = std::move( initial_string );
    parse_tags( string_reference, get_player_character(), *guy );
    return string_reference;

}

void follower_rules_ui_impl::print_hotkey( input_event &hotkey )
{
    // Padding spaces intentional, so it's obvious that the fake "Hotkey:" header refers to these.
    // TODO: Just reimplement everything as a table...? Would avoid this sort of thing.
    // But surely not *everything* needs to be a table...
    draw_colored_text( string_format( "  %s  ", static_cast<char>( hotkey.sequence.front() ) ),
                       c_green );
    ImGui::SameLine();
}

void follower_rules_ui_impl::copy_rules_popup( bool exporting_rules )
{
    if( ImGui::BeginPopup( "TEST" ) ) {
        draw_colored_text( string_format( _( "Import from: %s" ), guy->disp_name() ), c_yellow );
        ImGui::EndPopup();
    }
}

void follower_rules_ui_impl::draw_controls()
{
    if( ImGui::Button( "Copy?" ) ) {
        ImGui::OpenPopup( "TEST" );
        if( ImGui::BeginPopup( "TEST" ) ) {
            ImGui::Text("Copied!");
        }
    }
}
