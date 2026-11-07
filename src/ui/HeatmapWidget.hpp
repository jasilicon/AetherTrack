#pragma once

#include <gtkmm/drawingarea.h>

#include <cstdint>
#include <string>
#include <vector>

// Compact Cairo bar that reports the active skill's mastery level and the
// progress made toward its next milestone.
class MasteryBar : public Gtk::DrawingArea
{
public:
    MasteryBar();

    void set_total_seconds(int64_t total_seconds);
    void set_accent(const std::string& color_hex);

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    int64_t m_total_seconds{0};
    std::string m_accent{"#60A5FA"};
};

// 13x7 calendar heatmap. The vector contains one value per cell, oldest first,
// seven cells per column. Negative values (future days) are drawn as empty.
class HeatmapWidget : public Gtk::DrawingArea
{
public:
    HeatmapWidget();

    void set_days(std::vector<int64_t> seconds_per_day);
    void set_accent(const std::string& color_hex);

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    std::vector<int64_t> m_days;
    std::string m_accent{"#60A5FA"};
};
