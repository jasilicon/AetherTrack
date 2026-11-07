#pragma once

#include "core/Database.hpp"
#include "core/TimerEngine.hpp"
#include "ui/HeatmapWidget.hpp"
#include "ui/HoldButton.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/window.h>
#include <gtkmm/windowhandle.h>

#include <glibmm/refptr.h>
#include <sigc++/connection.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class MainWindow : public Gtk::Window
{
public:
    MainWindow();
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

private:
    void build_header();
    void build_deck();
    void build_insights();
    void build_pill();
    void setup_icon();
    void load_stylesheet();

    void initialize_data();
    void reload_skills(int64_t select_id);
    void on_activity_selected();
    void select_skill(int64_t skill_id);

    void refresh_ui();
    void refresh_insights();
    void sync_tick_timer();
    void attach_toplevel_monitor();
    [[nodiscard]] bool is_toplevel_minimized() const;
    void update_timer_font(std::size_t length);
    void update_lifetime_label(int64_t current_seconds);

    void on_start_pause();
    void on_trim();
    void on_reset_hold_complete();
    void confirm_reset(const Glib::ustring& note);

    void show_add_skill_popover();
    void add_skill();

    void toggle_insights();
    void set_insights_open(bool open);

    void set_mode(bool pill);
    void apply_always_on_top(bool enabled);

    Database m_database;
    std::unique_ptr<TimerEngine> m_engine;
    std::vector<Skill> m_skills;
    int64_t m_selected_skill_id{0};
    bool m_updating_activity{false};
    bool m_pill_mode{false};

    sigc::connection m_tick_connection;
    sigc::connection m_focus_idle;
    sigc::connection m_always_on_top_idle;
    sigc::connection m_toplevel_state_connection;

    // WindowHandle turns every non-interactive pixel of the window into a
    // drag surface, so the frameless deck and pill can be moved from anywhere.
    Gtk::WindowHandle m_drag_surface;
    Gtk::Box m_root{Gtk::Orientation::VERTICAL, 0};

    Gtk::Box m_titlebar{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Label m_title;
    Gtk::Box m_controls{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Button m_pill_collapse;
    Gtk::Button m_minimize;
    Gtk::Button m_close;

    // Main glass deck --------------------------------------------------------
    Gtk::Box m_deck{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box m_activity_row{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::DropDown m_activity_dropdown;
    Glib::RefPtr<Gtk::StringList> m_activity_model;
    Gtk::Button m_add_skill;
    Gtk::Label m_timer;
    Gtk::Label m_lifetime;
    Gtk::Box m_actions{Gtk::Orientation::VERTICAL, 10};
    Gtk::Button m_start_pause;
    Gtk::Button m_trim;
    HoldButton m_reset;

    // Insights drawer --------------------------------------------------------
    Gtk::Button m_insights_toggle;
    Gtk::Box m_insights{Gtk::Orientation::VERTICAL, 8};
    MasteryBar m_mastery;
    Gtk::Label m_heatmap_title;
    HeatmapWidget m_heatmap;
    Gtk::Label m_history_title;
    Gtk::ScrolledWindow m_history_scroll;
    Gtk::ListBox m_history_list;
    bool m_insights_open{false};

    // Micro pill -------------------------------------------------------------
    Gtk::Box m_pill{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Label m_pill_name;
    Gtk::Label m_pill_sep_1;
    Gtk::Label m_pill_timer;
    Gtk::Label m_pill_sep_2;
    Gtk::Button m_pill_toggle;
    Gtk::Button m_pill_expand;

    // Add-activity popover ---------------------------------------------------
    Gtk::Popover m_add_popover;
    Gtk::Box m_add_form{Gtk::Orientation::VERTICAL, 8};
    Gtk::Label m_add_title;
    Gtk::Entry m_add_entry;
    Gtk::Label m_add_error;
    Gtk::Box m_add_buttons{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_add_cancel;
    Gtk::Button m_add_confirm;

    // Reset-note popover -----------------------------------------------------
    Gtk::Popover m_reset_note_popover;
    Gtk::Box m_reset_form{Gtk::Orientation::VERTICAL, 6};
    Gtk::Entry m_reset_entry;
    Gtk::Label m_reset_hint;

    Glib::RefPtr<Gtk::CssProvider> m_css;
};
