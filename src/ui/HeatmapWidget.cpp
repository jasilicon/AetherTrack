#include "ui/HeatmapWidget.hpp"

#include <cairomm/context.h>
#include <cairomm/pattern.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string_view>

namespace
{
struct Rgb
{
    double r{0.376};
    double g{0.647};
    double b{0.980};
};

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

Rgb parse_hex(std::string_view value)
{
    Rgb color;
    if (value.size() != 7 || value.front() != '#') {
        return color;
    }

    std::array<int, 6> digits{};
    for (std::size_t i = 0; i < digits.size(); ++i) {
        digits[i] = hex_digit(value[i + 1]);
        if (digits[i] < 0) {
            return color;
        }
    }

    color.r = static_cast<double>(digits[0] * 16 + digits[1]) / 255.0;
    color.g = static_cast<double>(digits[2] * 16 + digits[3]) / 255.0;
    color.b = static_cast<double>(digits[4] * 16 + digits[5]) / 255.0;
    return color;
}

void rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr,
                  double x,
                  double y,
                  double width,
                  double height,
                  double radius)
{
    const double r = std::min(radius, std::min(width, height) / 2.0);
    const double pi = std::numbers::pi_v<double>;

    cr->begin_new_sub_path();
    cr->arc(x + width - r, y + r, r, -pi / 2.0, 0.0);
    cr->arc(x + width - r, y + height - r, r, 0.0, pi / 2.0);
    cr->arc(x + r, y + height - r, r, pi / 2.0, pi);
    cr->arc(x + r, y + r, r, pi, 3.0 * pi / 2.0);
    cr->close_path();
}

PangoLayout* create_layout(const Cairo::RefPtr<Cairo::Context>& cr,
                           const std::string& text,
                           double size,
                           bool bold)
{
    PangoLayout* layout = pango_cairo_create_layout(cr->cobj());
    pango_layout_set_text(layout, text.c_str(), -1);

    PangoFontDescription* description = pango_font_description_new();
    pango_font_description_set_family(description, "Inter");
    pango_font_description_set_absolute_size(description, size * PANGO_SCALE);
    pango_font_description_set_weight(description,
                                      bold ? PANGO_WEIGHT_SEMIBOLD
                                           : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);
    return layout;
}

int text_width(const Cairo::RefPtr<Cairo::Context>& cr,
               const std::string& text,
               double size,
               bool bold)
{
    PangoLayout* layout = create_layout(cr, text, size, bold);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    g_object_unref(layout);
    return width;
}

void draw_text(const Cairo::RefPtr<Cairo::Context>& cr,
               const std::string& text,
               double x,
               double y,
               double size,
               const Rgb& color,
               double alpha,
               bool bold)
{
    PangoLayout* layout = create_layout(cr, text, size, bold);
    cairo_set_source_rgba(cr->cobj(), color.r, color.g, color.b, alpha);
    cairo_move_to(cr->cobj(), x, y);
    pango_cairo_show_layout(cr->cobj(), layout);
    g_object_unref(layout);
}

std::string format_hours(int64_t seconds, bool decimals)
{
    const double hours = static_cast<double>(seconds) / 3600.0;
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals ? 1 : 0) << hours;
    return out.str();
}

constexpr std::array<int64_t, 5> kMasteryThresholds = {
    25LL * 3600,
    100LL * 3600,
    500LL * 3600,
    2500LL * 3600,
    10000LL * 3600,
};
} // namespace

MasteryBar::MasteryBar()
{
    add_css_class("mastery-bar");
    set_size_request(-1, 48);
    set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                         int width,
                         int height) { draw(cr, width, height); });
}

void MasteryBar::set_total_seconds(int64_t total_seconds)
{
    const int64_t clamped = std::max<int64_t>(0, total_seconds);
    if (m_total_seconds == clamped) {
        return;
    }
    m_total_seconds = clamped;
    queue_draw();
}

void MasteryBar::set_accent(const std::string& color_hex)
{
    if (m_accent == color_hex) {
        return;
    }
    m_accent = color_hex;
    queue_draw();
}

void MasteryBar::draw(const Cairo::RefPtr<Cairo::Context>& cr,
                      int width,
                      int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    const Rgb accent = parse_hex(m_accent);

    int level = 0;
    while (level < static_cast<int>(kMasteryThresholds.size()) &&
           m_total_seconds >= kMasteryThresholds[static_cast<std::size_t>(level)]) {
        ++level;
    }

    const bool mastered = level >= static_cast<int>(kMasteryThresholds.size());
    const int64_t lower = level == 0 ? 0 : kMasteryThresholds[static_cast<std::size_t>(level - 1)];
    const int64_t upper = mastered
        ? kMasteryThresholds.back()
        : kMasteryThresholds[static_cast<std::size_t>(level)];

    double fraction = 1.0;
    if (!mastered) {
        fraction = static_cast<double>(m_total_seconds - lower) /
                   static_cast<double>(upper - lower);
        fraction = std::clamp(fraction, 0.0, 1.0);
    }

    const std::string level_text = level == 0
        ? "Mastery \u00B7 Unranked"
        : "Mastery \u00B7 Level " + std::to_string(level);
    const std::string value_text = mastered
        ? "10,000+ hrs"
        : format_hours(m_total_seconds, true) + " / " +
              format_hours(upper, false) + " hrs";

    draw_text(cr, level_text, 0.0, 0.0, 9.5, Rgb{0.82, 0.85, 0.91}, 0.82, true);
    draw_text(cr,
              value_text,
              static_cast<double>(width) - static_cast<double>(text_width(cr, value_text, 9.0, false)),
              0.0,
              9.0,
              accent,
              0.88,
              false);

    const double bar_x = 0.0;
    const double bar_y = 23.0;
    const double bar_w = static_cast<double>(width);
    const double bar_h = 14.0;
    const double radius = bar_h / 2.0;

    rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, radius);
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.065);
    cr->fill_preserve();
    cr->set_line_width(1.0);
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.10);
    cr->stroke();

    if (fraction > 0.0) {
        const double fill_w = std::max(bar_h, bar_w * fraction);

        cr->save();
        rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, radius);
        cr->clip();
        cr->begin_new_path();
        cr->rectangle(bar_x, bar_y, fill_w, bar_h);

        auto gradient = Cairo::LinearGradient::create(bar_x, bar_y,
                                                      bar_x + bar_w, bar_y);
        gradient->add_color_stop_rgba(0.0, accent.r, accent.g, accent.b, 0.48);
        gradient->add_color_stop_rgba(0.65, accent.r, accent.g, accent.b, 0.82);
        gradient->add_color_stop_rgba(1.0, accent.r, accent.g, accent.b, 0.98);
        cr->set_source(gradient);
        cr->fill();
        cr->restore();

        if (mastered) {
            rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, radius);
            cr->set_line_width(1.0);
            cr->set_source_rgba(accent.r, accent.g, accent.b, 0.55);
            cr->stroke();
        }
    }
}

HeatmapWidget::HeatmapWidget()
{
    add_css_class("heatmap");
    set_size_request(-1, 112);
    set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                         int width,
                         int height) { draw(cr, width, height); });
}

void HeatmapWidget::set_days(std::vector<int64_t> seconds_per_day)
{
    m_days = std::move(seconds_per_day);
    queue_draw();
}

void HeatmapWidget::set_accent(const std::string& color_hex)
{
    if (m_accent == color_hex) {
        return;
    }
    m_accent = color_hex;
    queue_draw();
}

void HeatmapWidget::draw(const Cairo::RefPtr<Cairo::Context>& cr,
                         int width,
                         int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    constexpr int columns = 13;
    constexpr int rows = 7;
    constexpr double gap = 3.0;

    const double cell = std::min(
        (static_cast<double>(width) - (columns - 1) * gap) / columns,
        (static_cast<double>(height) - (rows - 1) * gap) / rows);

    if (cell < 2.0) {
        return;
    }

    const double grid_w = columns * cell + (columns - 1) * gap;
    const double grid_h = rows * cell + (rows - 1) * gap;
    const double origin_x = (static_cast<double>(width) - grid_w) / 2.0;
    const double origin_y = (static_cast<double>(height) - grid_h) / 2.0;
    const double radius = std::max(2.0, cell * 0.28);

    const Rgb accent = parse_hex(m_accent);

    for (std::size_t i = 0; i < m_days.size() && i < columns * rows; ++i) {
        const auto column = static_cast<int>(i) / rows;
        const auto row = static_cast<int>(i) % rows;
        const double x = origin_x + column * (cell + gap);
        const double y = origin_y + row * (cell + gap);

        if (m_days[i] < 0) {
            rounded_rect(cr, x, y, cell, cell, radius);
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.025);
            cr->fill();
            continue;
        }

        if (m_days[i] == 0) {
            rounded_rect(cr, x, y, cell, cell, radius);
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.04);
            cr->fill_preserve();
            cr->set_line_width(0.75);
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.075);
            cr->stroke();
            continue;
        }

        const double hours = static_cast<double>(m_days[i]) / 3600.0;
        const double intensity = std::clamp(std::pow(hours / 5.0, 0.55), 0.0, 1.0);
        const double alpha = 0.22 + 0.72 * intensity;

        rounded_rect(cr, x, y, cell, cell, radius);
        cr->set_source_rgba(accent.r, accent.g, accent.b, alpha);
        cr->fill();
    }
}
