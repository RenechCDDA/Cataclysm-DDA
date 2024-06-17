#include "game.h" // IWYU pragma: associated

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "color.h"
#include "debug.h"
#include "game.h"
#include "input_context.h"
#include "mission.h"
#include "npc.h"
#include "output.h"
#include "overmapbuffer.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units_utility.h"
#include "cata_imgui.h"
#include "imgui/imgui.h"

enum class mission_ui_tab_enum : int {
    ACTIVE = 0,
    COMPLETED,
    FAILED,
    num_tabs
};

static mission_ui_tab_enum &operator++( mission_ui_tab_enum &c )
{
    c = static_cast<mission_ui_tab_enum>( static_cast<int>( c ) + 1 );
    if( c == mission_ui_tab_enum::num_tabs ) {
        c = static_cast<mission_ui_tab_enum>( 0 );
    }
    return c;
}

static mission_ui_tab_enum &operator--( mission_ui_tab_enum &c )
{
    if( c == static_cast<mission_ui_tab_enum>( 0 ) ) {
        c = mission_ui_tab_enum::num_tabs;
    }
    c = static_cast<mission_ui_tab_enum>( static_cast<int>( c ) - 1 );
    return c;
}

class mission_ui;

class mission_ui
{
        friend class mission_ui_impl;
    public:
        void draw_mission_ui();
};

class mission_ui_impl : public cataimgui::window
{
    public:
        std::string last_action;
        explicit mission_ui_impl() : cataimgui::window( _( "Your missions" ),
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) {
        }

    private:
        void draw_mission_names( std::vector<mission *> missions, int &selected_mission,
                                 bool &need_adjust ) const;
        void draw_selected_description( std::vector<mission *> missions, int &selected_mission );

        mission_ui_tab_enum selected_tab = mission_ui_tab_enum::ACTIVE;
        mission_ui_tab_enum switch_tab = mission_ui_tab_enum::num_tabs;

        size_t window_width = str_width_to_pixels( TERMX ) / 2;
        size_t window_height = str_height_to_pixels( TERMY ) / 2;
        size_t table_column_width = window_width / 2;

    protected:
        void draw_controls() override;
};

void mission_ui::draw_mission_ui()
{
    input_context ctxt;
    mission_ui_impl p_impl;

    ctxt.register_navigate_ui_list();
    ctxt.register_leftright();
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "CONFIRM", to_translation( "Set selected mission as current objective" ) );
    // We don't actually have a way to remap this right now
    //ctxt.register_action( "DOUBLE_CLICK", to_translation( "Set selected mission as current objective" ) );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    // Smooths out our handling, makes tabs load immediately after input instead of waiting for next.
    ctxt.set_timeout( 10 );

    while( true ) {
        ui_manager::redraw_invalidated();


        p_impl.last_action = ctxt.handle_input();

        if( p_impl.last_action == "QUIT" ) {
            break;
        }
    }
}

void mission_ui_impl::draw_controls()
{
    ImGui::SetWindowSize( ImVec2( window_width, window_height ), ImGuiCond_Once );
    std::vector<mission *> umissions;

    static int selected_mission = 0;
    bool adjust_selected = false;
    if( ImGui::Button( "RESET DISPOSITION" ) ) {
        g->reset_npc_dispositions( false );
    }
    if( ImGui::Button( "RESET DISPOSITION (AVATAR)" ) ) {
        g->reset_npc_dispositions( true );
    }
    ImGui::Separator();
    int i = 0;
    draw_colored_text( "GAME follower ids" );
    for( character_id elem : g->follower_ids ) {
        i++;
        shared_ptr_fast<npc> npc_to_get = overmap_buffer.find_npc( elem );
        if( !npc_to_get )  {
            continue;
        }
        draw_colored_text( npc_to_get->name, c_green );
        ImGui::SameLine();
        draw_colored_text( std::to_string( elem.get_value() ), c_green );
        ImGui::SameLine();
        draw_colored_text( npc_to_get->global_omt_location().to_string(), c_green );
        ImGui::SameLine();
        ImGui::PushID( i );
        if( ImGui::Button( "Send me there!" ) ) {
            g->place_player_overmap( npc_to_get->global_omt_location() );
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    draw_colored_text( "AVATAR follower ids" );
    for( character_id elem : get_avatar().follower_ids ) {
        shared_ptr_fast<npc> npc_to_get = overmap_buffer.find_npc( elem );
        if( !npc_to_get )  {
            continue;
        }
        draw_colored_text( npc_to_get->name, c_yellow );
        ImGui::SameLine();
        draw_colored_text( std::to_string( elem.get_value() ), c_yellow );
        ImGui::SameLine();
        draw_colored_text( npc_to_get->global_omt_location().to_string(), c_green );
        ImGui::SameLine();
        ImGui::PushID( i );
        if( ImGui::Button( "Send me there!" ) ) {
            g->place_player_overmap( npc_to_get->global_omt_location() );
        }
        ImGui::PopID();
    }
}

void mission_ui_impl::draw_mission_names( std::vector<mission *> missions, int &selected_mission,
        bool &need_adjust ) const
{
    const int num_missions = missions.size();

    // roughly 6 lines of header info. title+tab+objective+table headers+2 lines worth of padding between those four
    const float header_height = ImGui::GetTextLineHeight() * 6;
    if( ImGui::BeginListBox( "##LISTBOX", ImVec2( table_column_width * 0.75,
                             window_height - header_height ) ) ) {
        for( int i = 0; i < num_missions; i++ ) {
            const bool is_selected = selected_mission == i;
            ImGui::PushID( i );
            if( ImGui::Selectable( missions[i]->name().c_str(), is_selected,
                                   ImGuiSelectableFlags_AllowDoubleClick ) ) {
                selected_mission = i;
                if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) {
                    get_avatar().set_active_mission( *missions[selected_mission] );
                }
            }

            if( is_selected && need_adjust ) {
                ImGui::SetScrollHereY();
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
}

void mission_ui_impl::draw_selected_description( std::vector<mission *> missions,
        int &selected_mission )
{
    mission *miss = missions[selected_mission];
    ImGui::TextWrapped( _( "Mission: %s" ), miss->name().c_str() );
    if( miss->get_npc_id().is_valid() ) {
        npc *guy = g->find_npc( miss->get_npc_id() );
        if( guy ) {
            ImGui::TextWrapped( _( "Given by: %s" ), guy->disp_name().c_str() );
        }
    }
    ImGui::Separator();
    static std::string raw_description;
    static std::string parsed_description;
    // Avoid replacing expanded snippets with other valid snippet text on redraw
    if( raw_description != miss->get_description() ) {
        raw_description = miss->get_description();
        parsed_description = raw_description;
        // Handles(example)  <reward_count:item>   in description
        // Not handled in parse_tags for some reason!
        dialogue d( get_talker_for( get_avatar() ), nullptr, {} );
        const talk_effect_fun_t::likely_rewards_t &rewards = miss->get_likely_rewards();
        for( const auto &reward : rewards ) {
            std::string token = "<reward_count:" + itype_id( reward.second.evaluate( d ) ).str() + ">";
            parsed_description = string_replace( parsed_description, token, string_format( "%g",
                                                 reward.first.evaluate( d ) ) );
        }
        parse_tags( parsed_description, get_player_character(), get_player_character() );
    }
    draw_colored_text( parsed_description, c_unset, table_column_width * 1.15 );
    if( miss->has_deadline() ) {
        const time_point deadline = miss->get_deadline();
        ImGui::Text( _( "Deadline: %s" ), to_string( deadline ).c_str() );
        if( selected_tab == mission_ui_tab_enum::ACTIVE ) {
            // There's no point in displaying this for a completed/failed mission.
            // @ TODO: But displaying when you completed it would be useful.
            const time_duration remaining = deadline - calendar::turn;
            std::string remaining_time;
            if( remaining <= 0_turns ) {
                remaining_time = _( "None!" );
            } else if( get_player_character().has_watch() ) {
                remaining_time = to_string( remaining );
            } else {
                remaining_time = to_string_approx( remaining );
            }
            ImGui::TextWrapped( _( "Time remaining: %s" ), remaining_time.c_str() );
        }
    }
    if( miss->has_target() ) {
        // TODO: target does not contain a z-component, targets are assumed to be on z=0
        const tripoint_abs_omt pos = get_player_character().global_omt_location();
        draw_colored_text( string_format( _( "Target: %s" ), miss->get_target().to_string() ), c_white );
        // Below is done instead of a table for the benefit of right-to-left languages
        //~Extra padding spaces in the English text are so that the replaced string vertically aligns with the one above
        draw_colored_text( string_format( _( "You:    %s" ), pos.to_string() ), c_white );
        int omt_distance = rl_dist( pos, miss->get_target() );
        if( omt_distance > 0 ) {
            // One OMT is 24 tiles across, at 1x1 meters each, so we can simply do number of OMTs * 24
            units::length actual_distance = omt_distance * 24_meter;
            //~Paranthesis is a real-world value for distance. Example string: "Distance: 223 tiles (5352 m)"
            draw_colored_text( string_format( _( "Distance: %1$s tiles (%2$s)" ),
                                              omt_distance, length_to_string_approx( actual_distance ) ), c_white );
        }
    }
}

void game::list_missions()
{
    mission_ui new_instance;
    new_instance.draw_mission_ui();
}
