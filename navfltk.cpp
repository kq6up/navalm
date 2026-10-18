/* SPDX-License-Identifier: MIT
 * Native FLTK workbench over NAVALM's unchanged C calculation engine.
 */
#include "navplatform.h"

#include "nav_version.h"
#include "navdownload.h"
#include "navfile.h"
#include "navfltk_map.h"
#include "navfltk_plot.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
namespace fs = std::filesystem;
static fs::path application_root, application_resources;
using namespace desktop;
static Fl_Color navy = fl_rgb_color(21, 43, 57), teal = fl_rgb_color(0, 119, 128),
                paper = fl_rgb_color(245, 247, 246);
static std::string fmt(double v, int precision = 6) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(precision) << v;
    return s.str();
}
static std::string capture(std::function<void(FILE *)> f) {
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::tmpfile(), &std::fclose);
    FILE *p = file.get();
    if (!p)
        throw std::runtime_error("Cannot create a temporary report.");
    f(p);
    std::rewind(p);
    std::string s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, p)))
        s.append(b, n);
    return s;
}
static void write_file(const std::string &path, const std::string &data) {
    std::ofstream f(path, std::ios::binary);
    if (!f || !(f << data) || !f.flush())
        throw std::runtime_error("Cannot write " + path);
}
static std::string choose_file(const char *title, const char *filter, bool save,
                               const char *name = "") {
    Fl_Native_File_Chooser f;
    f.title(title);
    f.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE : Fl_Native_File_Chooser::BROWSE_FILE);
    f.filter(filter);
    if (save) {
        f.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
        f.preset_file(name);
    }
    if (f.show())
        return "";
    return f.filename();
}
class Plot : public Fl_Widget {
  public:
    Overlay scene;
    std::shared_ptr<LandMap> land_map;
    bool map_visible = true;
    const LandMap *cached_map = nullptr;
    std::array<double, 6> cached_key{};
    LandContours land_cache;
    bool ready = false, layer_visible[4] = {true, true, true, true};
    double opacity = 1, zoom = 1, pan_x = 0, pan_y = 0;
    int drag_x = 0, drag_y = 0;
    bool dragging = false;
    std::function<void()> view_changed;
    Plot(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void changed() {
        redraw();
        if (view_changed)
            view_changed();
    }
    void fit_view() {
        zoom = 1;
        pan_x = pan_y = 0;
        changed();
    }
    void zoom_at(double factor, double anchor_x, double anchor_y) {
        double next = std::clamp(zoom * factor, .25, 8.0), ratio = next / zoom;
        pan_x = anchor_x - (x() + w() / 2.) - (anchor_x - (x() + w() / 2.) - pan_x) * ratio;
        pan_y = anchor_y - (y() + h() / 2.) - (anchor_y - (y() + h() / 2.) - pan_y) * ratio;
        zoom = next;
        changed();
    }
    void zoom_by(double factor) { zoom_at(factor, x() + w() / 2., y() + h() / 2.); }
    int handle(int event) override {
        if (!ready)
            return Fl_Widget::handle(event);
        switch (event) {
        case FL_FOCUS:
        case FL_UNFOCUS:
        case FL_ENTER:
            return 1;
        case FL_PUSH:
            if (Fl::event_button() != FL_LEFT_MOUSE)
                return 0;
            take_focus();
            if (Fl::event_clicks()) {
                fit_view();
                return 1;
            }
            drag_x = Fl::event_x();
            drag_y = Fl::event_y();
            dragging = true;
            return 1;
        case FL_DRAG:
            if (!dragging)
                return 0;
            pan_x += Fl::event_x() - drag_x;
            pan_y += Fl::event_y() - drag_y;
            drag_x = Fl::event_x();
            drag_y = Fl::event_y();
            changed();
            return 1;
        case FL_RELEASE:
            dragging = false;
            return 1;
        case FL_MOUSEWHEEL:
            zoom_at(std::pow(1.15, -Fl::event_dy()), Fl::event_x(), Fl::event_y());
            return 1;
        case FL_KEYDOWN:
            if (Fl::event_key() == '+' || Fl::event_key() == '=' ||
                Fl::event_key() == FL_KP + '+') {
                zoom_by(1.25);
                return 1;
            }
            if (Fl::event_key() == '-' || Fl::event_key() == FL_KP + '-') {
                zoom_by(.8);
                return 1;
            }
            if (Fl::event_key() == '0' || Fl::event_key() == FL_Home) {
                fit_view();
                return 1;
            }
            break;
        }
        return Fl_Widget::handle(event);
    }
    void draw() override {
        fl_push_clip(x(), y(), w(), h());
        fl_color(paper);
        fl_rectf(x(), y(), w(), h());
        if (!ready) {
            fl_color(navy);
            fl_font(FL_HELVETICA, 16);
            fl_draw("Select saved sights and calculate a fix.", x(), y(), w(), h(),
                    FL_ALIGN_CENTER);
            fl_pop_clip();
            return;
        }
        auto sheet = make_sheet(scene);
        double scale = std::min((w() - 16) / Sheet::width, (h() - 16) / Sheet::height) * zoom;
        double left = x() + (w() - Sheet::width * scale) / 2 + pan_x;
        double top = y() + (h() - Sheet::height * scale) / 2 + pan_y;
        fl_color(FL_WHITE);
        fl_rectf((int)left, (int)top, (int)(Sheet::width * scale), (int)(Sheet::height * scale));
        auto color = [&](const std::string &hex, double alpha = 1) {
            unsigned r = 0, g = 0, b = 0;
            std::sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b);
            fl_color(fl_color_average(fl_rgb_color(r, g, b), FL_WHITE, (float)(alpha * opacity)));
        };
        auto clip = [&](int index) {
            if (index >= 0) {
                auto b = sheet.clips[index];
                fl_push_clip((int)std::ceil(left + b.x * scale), (int)std::ceil(top + b.y * scale),
                             (int)std::floor(b.w * scale), (int)std::floor(b.h * scale));
            }
        };
        if (land_map && map_visible) {
            std::array<double, 6> key{scene.lat, scene.lon, scene.grid_lat,
                                      sheet.ce,  sheet.cn,  sheet.scale};
            if (cached_map != land_map.get() || key != cached_key) {
                land_cache = map_contours(*land_map, scene, sheet);
                cached_map = land_map.get();
                cached_key = key;
            }
            clip(0);
            fl_push_matrix();
            fl_translate(left, top);
            fl_scale(scale, scale);
            auto b = sheet.clips[0];
            fl_color(fl_rgb_color(230, 241, 246));
            fl_begin_polygon();
            fl_vertex(b.x, b.y);
            fl_vertex(b.x + b.w, b.y);
            fl_vertex(b.x + b.w, b.y + b.h);
            fl_vertex(b.x, b.y + b.h);
            fl_end_polygon();
            for (const auto &polygon : land_cache) {
                fl_color(fl_rgb_color(227, 222, 199));
                fl_begin_complex_polygon();
                for (const auto &ring : polygon) {
                    for (auto p : ring)
                        fl_vertex(p.x, p.y);
                    fl_gap();
                }
                fl_end_complex_polygon();
                fl_color(fl_rgb_color(105, 122, 115));
                fl_line_style(FL_SOLID, 1);
                for (const auto &ring : polygon) {
                    fl_begin_loop();
                    for (auto p : ring)
                        fl_vertex(p.x, p.y);
                    fl_end_loop();
                }
            }
            fl_line_style(FL_SOLID);
            fl_pop_matrix();
            fl_pop_clip();
            sheet.text({46, 718}, land_map->name, 0, -1, 9.5, "#5a6b74");
            sheet.text({46, 733},
                       "Offline geographic context; depth and hazard symbols are not included.", 0,
                       -1, 9, "#5a6b74");
        }
        for (auto item : sheet.order) {
            int layer =
                item.first ? sheet.captions[item.second].layer : sheet.marks[item.second].layer;
            if (!layer_visible[layer])
                continue;
            if (!item.first) {
                const auto &mark = sheet.marks[item.second];
                if (mark.points.empty())
                    continue;
                clip(mark.clip);
                fl_push_matrix();
                fl_translate(left, top);
                fl_scale(scale, scale);
                if (mark.ink.fill != "none") {
                    color(mark.ink.fill, mark.ink.alpha * mark.ink.fill_alpha);
                    fl_begin_polygon();
                    for (auto p : mark.points)
                        fl_vertex(p.x, p.y);
                    fl_end_polygon();
                }
                if (mark.ink.stroke != "none") {
                    color(mark.ink.stroke, mark.ink.alpha);
                    char dash[3] = {};
                    if (!mark.ink.dash.empty()) {
                        int on = 0, off = 0;
                        std::sscanf(mark.ink.dash.c_str(), "%d %d", &on, &off);
                        dash[0] = (char)std::max(1, (int)std::round(on * scale));
                        dash[1] = (char)std::max(1, (int)std::round(off * scale));
                    }
                    fl_line_style(FL_SOLID, std::max(1, (int)std::round(mark.ink.width * scale)),
                                  dash[0] ? dash : nullptr);
                    if (mark.closed)
                        fl_begin_loop();
                    else
                        fl_begin_line();
                    for (auto p : mark.points)
                        fl_vertex(p.x, p.y);
                    if (mark.closed)
                        fl_end_loop();
                    else
                        fl_end_line();
                }
                fl_pop_matrix();
                if (mark.clip >= 0)
                    fl_pop_clip();
            } else {
                fl_line_style(FL_SOLID);
                const auto &label = sheet.captions[item.second];
                clip(label.clip);
                Fl_Font font = label.mono ? FL_COURIER : FL_HELVETICA;
                if (label.bold)
                    font += 1;
                if (label.italic)
                    font += 2;
                fl_font(font, std::max(5, (int)std::round(label.size * scale)));
                double tx = left + label.point.x * scale, ty = top + label.point.y * scale;
                if (label.anchor == "middle")
                    tx -= fl_width(label.text.c_str()) / 2;
                if (label.anchor == "end")
                    tx -= fl_width(label.text.c_str());
                if (label.halo) {
                    fl_color(FL_WHITE);
                    for (int i = 0; i < 8; ++i) {
                        double a = i * M_PI / 4;
                        fl_draw(label.text.c_str(),
                                (int)std::round(tx + 1.75 * scale * std::cos(a)),
                                (int)std::round(ty + 1.75 * scale * std::sin(a)));
                    }
                }
                color(label.color);
                fl_draw(label.text.c_str(), (int)std::round(tx), (int)std::round(ty));
                if (label.clip >= 0)
                    fl_pop_clip();
            }
        }
        fl_line_style(FL_SOLID);
        fl_pop_clip();
    }
};
class Workbench {
  public:
    Fl_Double_Window win{1180, 850, "NAVALM — Navigation Workbench"};
    Fl_Tabs *tabs;
    Fl_Group *almanac_tab, *sight_tab, *fix_tab, *chart_tab;
    Fl_Box *status, *engine_status, *headline, *paths;
    Fl_Choice *engine, *body, *star, *horizon, *limb, *arc, *units, *lat_hemi, *lon_hemi, *al_body,
        *al_star, *al_lat_hemi, *al_lon_hemi;
    Fl_Input *date, *time, *hs, *label, *lat, *lon, *ie, *height, *temp, *press, *course, *speed,
        *set, *drift;
    Fl_Input *al_date, *al_time, *al_lat, *al_lon, *fix_date, *fix_time;
    Fl_Check_Button *motion, *fix_at, *layers[4], *map_toggle;
    Fl_Value_Slider *opacity;
    Fl_Multi_Browser *saved;
    Plot *plot;
    std::unique_ptr<Fl_Double_Window> expanded_window;
    Plot *expanded_plot = nullptr;
    Fl_Box *detached_hint, *zoom_readout = nullptr, *expanded_zoom_readout = nullptr;
    Fl_Button *detach_button, *download_map_button, *cancel_map_button;
    Fl_Box *map_information;
    std::shared_ptr<LandMap> land_map;
    std::thread map_worker;
    std::atomic<bool> map_cancel{false}, map_done{false};
    std::atomic<uint64_t> map_bytes{0};
    bool map_downloading = false;
    std::string map_download_error;
    std::shared_ptr<LandMap> downloaded_map;
    Fl_Text_Buffer al_buffer, report_buffer, fix_buffer, chart_buffer;
    Fl_Text_Display *al_display, *report_display, *fix_display, *chart_display;
    SightStore store{};
    int edit_id = 0;
    bool testing = false;
    std::vector<Sight> fix_sights;
    std::vector<Reduction> fix_red;
    FixResult fix_result{};
    std::vector<std::unique_ptr<std::function<void()>>> callbacks;
    Fl_Button *reduce_button, *save_button, *update_button, *fix_button;
    Workbench() {
        Fl::scheme("gtk+");
        Fl::background(245, 247, 246);
        Fl::background2(255, 255, 255);
        Fl::foreground(25, 47, 60);
#ifdef _WIN32
        // FLTK GDI font names start with a style marker: space, B, I or P.
        Fl::set_font(FL_HELVETICA, " Segoe UI");
        Fl::set_font(FL_HELVETICA_BOLD, "BSegoe UI");
        Fl::set_font(FL_HELVETICA_ITALIC, "ISegoe UI");
        Fl::set_font(FL_HELVETICA_BOLD_ITALIC, "PSegoe UI");
        Fl::set_font(FL_COURIER, " Consolas");
        Fl::set_font(FL_COURIER_BOLD, "BConsolas");
        Fl::set_font(FL_COURIER_ITALIC, "IConsolas");
        Fl::set_font(FL_COURIER_BOLD_ITALIC, "PConsolas");
#else
        Fl::set_font(FL_HELVETICA, "Helvetica");
        Fl::set_font(FL_COURIER, "Menlo");
        Fl::set_font(FL_COURIER_BOLD, "Menlo-Bold");
        Fl::set_font(FL_COURIER_ITALIC, "Menlo-Italic");
        Fl::set_font(FL_COURIER_BOLD_ITALIC, "Menlo-BoldItalic");
#endif
        win.begin();
        auto *header = new Fl_Box(0, 0, 1180, 82);
        header->box(FL_FLAT_BOX);
        header->color(navy);
        auto *name = new Fl_Box(26, 12, 500, 29, "NAVALM  /  Navigation Workbench");
        name->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        name->labelcolor(FL_WHITE);
        name->labelfont(FL_HELVETICA_BOLD);
        name->labelsize(22);
        auto *sub =
            new Fl_Box(26, 45, 650, 23,
                       "CELESTIAL NAVIGATION    /    ALMANAC · SIGHTS · UNIVERSAL PLOTTING CHART");
        sub->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        sub->labelcolor(fl_rgb_color(169, 204, 210));
        sub->labelsize(11);
        engine = choice(870, 30, 278, "Astronomy engine",
                        "Precise — NOVAS + JPL DE440s|Classic — XEphem");
        engine->labelcolor(FL_WHITE);
        bind(engine, [this] { change_engine(); });
        tabs = new Fl_Tabs(16, 96, 1148, 706);
        tabs->selection_color(FL_WHITE);
        tabs->labelsize(15);
        almanac_tab = new Fl_Group(16, 130, 1148, 672, "Almanac");
        al_date = input(36, 166, 195, "UTC date · YYYY-MM-DD");
        al_time = input(251, 166, 180, "UTC time · HH:MM:SS");
        button(451, 166, 95, "Now UTC", [this] { now(al_date, al_time); });
        al_body = choice(566, 166, 240, "Object",
                         "Sun|Moon|Mercury|Venus|Mars|Jupiter|Saturn|Aries|Navigation star");
        al_star = choice(826, 166, 315, "Navigation star", "");
        fill_stars(al_star);
        al_lat = input(36, 232, 190, "DR latitude · DD MM.m");
        al_lat_hemi = choice(236, 232, 68, "", "N|S");
        al_lon = input(324, 232, 190, "DR longitude · DDD MM.m");
        al_lon_hemi = choice(524, 232, 68, "", "E|W");
        button(616, 232, 160, "Calculate almanac", [this] { almanac(false); }, true);
        button(792, 232, 160, "Hourly table", [this] { almanac(true); });
        button(968, 232, 173, "Export text…", [this] { export_text(al_buffer, "almanac.txt"); });
        al_display = display(36, 300, 1105, 475, al_buffer);
        auto *al_angle_hint = new Fl_Box(36, 270, 1105, 24,
                                         "Angles: degrees, a SPACE, then minutes (34 02.5 / 117 "
                                         "21.9). Decimal degrees also accepted.");
        al_angle_hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        al_angle_hint->labelsize(12);
        almanac_tab->resizable(al_display);
        almanac_tab->end();
        sight_tab = new Fl_Group(16, 130, 1148, 672, "Sight reduction");
        body = choice(36, 168, 210, "Observed body",
                      "Sun|Moon|Mercury|Venus|Mars|Jupiter|Saturn|Navigation star");
        star = choice(266, 168, 210, "Navigation star", "");
        fill_stars(star);
        date = input(36, 230, 210, "UTC date · YYYY-MM-DD");
        time = input(266, 230, 210, "UTC time · HH:MM:SS");
        hs = input(36, 292, 210, "Sextant Hs · DD MM.m");
        label = input(266, 292, 210, "Sight label");
        label->maximum_size(NAVSIGHT_LABEL - 1);
        lat = input(36, 354, 132, "DR latitude · DD MM.m");
        lat_hemi = choice(178, 354, 68, "", "N|S");
        lon = input(266, 354, 132, "DR longitude · DDD MM.m");
        lon_hemi = choice(408, 354, 68, "", "E|W");
        horizon = choice(36, 416, 210, "Horizon", "Sea horizon|Artificial horizon (halve Hs)");
        limb = choice(266, 416, 210, "Limb", "Lower limb|Center|Upper limb");
        ie = input(36, 478, 100, "Index error · min");
        arc = choice(146, 478, 100, "Index error arc", "Off arc|On arc");
        height = input(266, 478, 110, "Height of eye");
        units = choice(386, 478, 90, "Units", "Feet|Metres");
        temp = input(36, 540, 210, "Temperature · Celsius");
        press = input(266, 540, 210, "Pressure · millibars");
        motion = new Fl_Check_Button(36, 584, 430, 25,
                                     "Advance this LOP using vessel motion and current");
        motion->labelsize(13);
        course = input(36, 640, 100, "Course · true");
        speed = input(149, 640, 100, "Speed · kn");
        set = input(262, 640, 100, "Set · true");
        drift = input(375, 640, 101, "Drift · kn");
        reduce_button = button(36, 696, 130, "Reduce sight", [this] { reduce_sight(); }, true);
        save_button = button(178, 696, 140, "Save as new", [this] { save_sight(false); });
        update_button = button(330, 696, 146, "Update edited", [this] { save_sight(true); });
        button(36, 744, 140, "New / defaults", [this] { new_sight(); });
        button(188, 744, 136, "Sun example", [this] { example(); });
        button(336, 744, 140, "Save defaults", [this] { save_defaults(); });
        headline = new Fl_Box(505, 144, 632, 35, "Enter an observation, then reduce the sight.");
        headline->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        headline->labelfont(FL_HELVETICA_BOLD);
        headline->labelsize(17);
        headline->labelcolor(teal);
        report_display = display(505, 188, 632, 540, report_buffer);
        button(505, 744, 180, "Export worked report…",
               [this] { export_text(report_buffer, "sight-reduction.txt"); });
        auto *input_hint = new Fl_Box(703, 738, 434, 45,
                                      "Angles: degrees SPACE minutes, e.g. 117 21.9.\nDecimal "
                                      "degrees also accepted. All times are UTC.");
        input_hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        input_hint->labelsize(12);
        sight_tab->resizable(report_display);
        sight_tab->end();
        fix_tab = new Fl_Group(16, 130, 1148, 672, "Saved sights + plotting");
        auto *hint = new Fl_Box(36, 140, 430, 40,
                                "Select 2–16 sights for a stationary or running fix.\nUse Command "
                                "/ Shift to select multiple rows.");
#ifdef _WIN32
        hint->copy_label("Select 2–16 sights for a stationary or running fix.\nUse Control / Shift "
                         "to select multiple rows.");
#endif
        hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        hint->labelsize(13);
        saved = new Fl_Multi_Browser(36, 190, 430, 215);
        saved->textsize(13);
        saved->textfont(FL_COURIER);
        saved->format_char('\0');
        button(36, 420, 94, "Edit sight", [this] { edit_sight(); });
        button(142, 420, 94, "Delete…", [this] { delete_sights(); });
        button(248, 420, 100, "Select all", [this] {
            for (int i = 1; i <= saved->size(); ++i)
                saved->select(i);
        });
        button(360, 420, 106, "Reload log", [this] { load_store(); });
        fix_at = new Fl_Check_Button(36, 469, 430, 27,
                                     "Advance to explicit UTC fix time (otherwise latest sight)");
        fix_at->labelsize(12);
        fix_date = input(36, 530, 210, "Fix date · YYYY-MM-DD");
        fix_time = input(266, 530, 200, "Fix time · HH:MM:SS");
        fix_button =
            button(36, 579, 208, "Calculate selected fix", [this] { calculate_fix(); }, true);
        button(256, 579, 210, "Export fix report…", [this] { export_text(fix_buffer, "fix.txt"); });
        fix_display = display(36, 629, 430, 146, fix_buffer);
        fix_display->textsize(12);
        map_toggle = new Fl_Check_Button(493, 143, 82, 28, "Map");
        map_toggle->labelsize(12);
        map_toggle->value(1);
        bind(map_toggle, [this] {
            update_map_views();
            save_map_setting();
        });
        const char *names[] = {"Grid / rose", "LOPs / motion", "Labels", "Fix / detail"};
        for (int i = 0; i < 4; ++i) {
            layers[i] = new Fl_Check_Button(575 + i * 141, 143, 141, 28, names[i]);
            layers[i]->labelsize(12);
            layers[i]->value(1);
            bind(layers[i], [this] { update_layers(); });
        }
        detached_hint = new Fl_Box(
            493, 183, 645, 512,
            "Plot detached into its own window.\nChoose Reattach sheet to return it here.");
        detached_hint->labelsize(15);
        detached_hint->hide();
        plot = new Plot(493, 183, 645, 512);
        plot->tooltip(
            "Mouse wheel: zoom at pointer. Drag: pan. Double-click or 0: fit. + / -: zoom.");
        plot->view_changed = [this] { update_zoom_readout(); };
        opacity = new Fl_Value_Slider(560, 711, 204, 27, "Opacity");
        opacity->type(FL_HOR_NICE_SLIDER);
        opacity->align(FL_ALIGN_LEFT);
        opacity->bounds(.1, 1);
        opacity->step(.05);
        opacity->value(1);
        opacity->textsize(12);
        bind(opacity, [this] { update_layers(); });
        button(787, 708, 171, "Transparent SVG…", [this] { export_overlay(false); });
        button(969, 708, 169, "Geographic vectors…", [this] { export_overlay(true); });
        detach_button = button(493, 749, 155, "Detach sheet…", [this] {
            if (expanded_window && expanded_window->shown())
                reattach_plot();
            else
                expand_plot();
        });
        button(663, 749, 45, "−", [this] { active_plot()->zoom_by(.8); });
        button(716, 749, 65, "Fit", [this] { active_plot()->fit_view(); });
        button(789, 749, 45, "+", [this] { active_plot()->zoom_by(1.25); });
        zoom_readout = new Fl_Box(842, 749, 78, 34, "100%");
        zoom_readout->labelsize(13);
        auto *plot_hint = new Fl_Box(925, 749, 213, 34, "Wheel zoom · drag to pan");
        plot_hint->labelsize(11);
        fix_tab->resizable(plot);
        fix_tab->end();
        chart_tab = new Fl_Group(16, 130, 1148, 672, "Charts + setup");
        engine_status = new Fl_Box(36, 147, 1105, 55);
        engine_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        engine_status->labelsize(14);
        button(36, 220, 245, "Choose JPL ephemeris (.nav)…", [this] { choose_ephemeris(); });
        paths = new Fl_Box(303, 211, 838, 80);
        paths->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        paths->labelsize(12);
        download_map_button =
            button(36, 310, 210, "Download world map", [this] { start_map_download(); }, true);
        button(262, 310, 210, "Use OpenCPN coastline", [this] { use_opencpn_map(); });
        button(488, 310, 210, "Load land map (.shp)…", [this] {
            auto path =
                choose_file("Choose a WGS84 land polygon shapefile", "Shapefile\t*.shp", false);
            if (!path.empty())
                set_land_map(load_land_map(path));
        });
        cancel_map_button = button(714, 310, 120, "Cancel", [this] { map_cancel = true; });
        cancel_map_button->deactivate();
        map_information = new Fl_Box(36, 352, 1105, 66);
        map_information->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        map_information->labelsize(13);
        chart_display = display(36, 430, 1105, 345, chart_buffer);
        auto *charts = chart_display;
        charts->textsize(14);
        chart_buffer.text(
            "OFFLINE MAP UNDER THE UNIVERSAL PLOTTING SHEET\n\n"
            "Download world map installs Natural Earth 1:10 million land and coastline\n"
            "(about 7 MB). Once installed, it works without an internet connection.\n"
            "Natural Earth is public domain: naturalearthdata.com.\n\n"
            "Use OpenCPN coastline reads OpenCPN's chart folders and loads its OSM\n"
            "medium/low basemap without changing OpenCPN or copying its ENC charts.\n"
            "Load land map accepts WGS84 Polygon/PolygonZ .shp files up to 256 MB.\n"
            "Large full-resolution files are intentionally excluded; choose medium or low.\n\n"
            "Map, UGP grid and sight vectors share the solver's local geographic plane.\n"
            "The Map checkbox toggles the underlay in both docked and detached views.\n"
            "Pan/zoom and fix recalculation keep them aligned. Transparent SVG and\n"
            "GeoJSON exports still contain the plotting vectors without a background map.\n\n"
            "This is a land/coastline map, not an ENC/BSB chart renderer: it has no\n"
            "depths, soundings, aids to navigation or chart updates.\n\n"
            "Saved sights and defaults stay in the .local folder beside the app.\n"
            "Calculations continue to use NAVALM's existing C engine.");
        chart_tab->resizable(charts);
        chart_tab->end();
        tabs->end();
        status = new Fl_Box(22, 811, 1136, 28, "Ready");
        status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        status->labelsize(12);
        win.resizable(tabs);
        win.size_range(1180, 850);
        win.end();
        win.callback(
            [](Fl_Widget *w, void *p) {
                auto *app = static_cast<Workbench *>(p);
                if (app->expanded_window)
                    app->expanded_window->hide();
                w->hide();
            },
            this);
        new_sight();
        now(al_date, al_time);
        al_lat->value("00 00.0");
        al_lon->value("000 00.0");
        for (auto *field : {lat, al_lat})
            field->tooltip("Degrees SPACE minutes: 34 02.5 = 34 degrees 2.5 minutes.\nDecimal "
                           "degrees are also accepted. Select N or S separately.");
        for (auto *field : {lon, al_lon})
            field->tooltip("Degrees SPACE minutes: 117 21.9 = 117 degrees 21.9 minutes.\nDecimal "
                           "degrees are also accepted. Select E or W separately.");
        hs->tooltip("Degrees SPACE minutes: 94 37.2 = 94 degrees 37.2 minutes.\nDecimal degrees "
                    "are also accepted. For an artificial horizon enter the full sextant angle.");
        now(fix_date, fix_time);
        bind(body, [this] { body_state(); });
        bind(al_body, [this] { body_state(); });
        bind(horizon, [this] { body_state(); });
        bind(motion, [this] { body_state(); });
        bind(fix_at, [this] { body_state(); });
        body_state();
        load_store();
        engine->value(nav_engine_current() == NAV_ENGINE_PRECISE ? 0 : 1);
        engine_info();
        load_map_setting();
    }
    ~Workbench() {
        Fl::remove_timeout(map_download_tick, this);
        map_cancel = true;
        if (map_worker.joinable())
            map_worker.join();
        al_display->buffer(nullptr);
        report_display->buffer(nullptr);
        fix_display->buffer(nullptr);
        chart_display->buffer(nullptr);
        navsight_store_free(&store);
    }
    void bind(Fl_Widget *w, std::function<void()> fn) {
        auto f = std::make_unique<std::function<void()>>([this, fn] {
            try {
                fn();
            } catch (const std::exception &e) {
                message(e.what(), true);
            }
        });
        w->callback([](Fl_Widget *, void *p) { (*static_cast<std::function<void()> *>(p))(); },
                    f.get());
        callbacks.push_back(std::move(f));
    }
    Fl_Input *input(int x, int y, int w, const char *name) {
        auto *p = new Fl_Input(x, y, w, 32, name);
        p->align(FL_ALIGN_TOP_LEFT);
        p->labelsize(12);
        p->textsize(14);
        return p;
    }
    Fl_Choice *choice(int x, int y, int w, const char *name, const char *items) {
        auto *p = new Fl_Choice(x, y, w, 32, name);
        p->align(FL_ALIGN_TOP_LEFT);
        p->labelsize(12);
        p->textsize(13);
        if (*items)
            p->add(items);
        p->value(0);
        return p;
    }
    Fl_Button *button(int x, int y, int w, const char *name, std::function<void()> fn,
                      bool primary = false) {
        auto *p = new Fl_Button(x, y, w, 34, name);
        p->labelsize(13);
        if (primary) {
            p->color(teal);
            p->labelcolor(FL_WHITE);
        }
        bind(p, fn);
        return p;
    }
    Fl_Text_Display *display(int x, int y, int w, int h, Fl_Text_Buffer &buffer) {
        auto *p = new Fl_Text_Display(x, y, w, h);
        p->buffer(buffer);
        p->textfont(FL_COURIER);
        p->textsize(13);
        return p;
    }
    void message(const std::string &s, bool error = false) {
        status->copy_label(s.c_str());
        status->labelcolor(error ? fl_rgb_color(172, 49, 53) : navy);
        if (error && !testing)
            fl_alert("%s", s.c_str());
    }
    void fill_stars(Fl_Choice *p) {
        for (size_t i = 0; i < nav_star_count_get(); ++i)
            p->add(nav_star_name(i));
        p->value(0);
    }
    void now(Fl_Input *d, Fl_Input *t) {
        std::time_t n = std::time(nullptr);
        auto g = *std::gmtime(&n);
        char b[40];
        std::strftime(b, sizeof b, "%Y-%m-%d", &g);
        d->value(b);
        std::strftime(b, sizeof b, "%H:%M:%S", &g);
        t->value(b);
    }
    void body_state() {
        if (body->value() == 7)
            star->activate();
        else
            star->deactivate();
        if (al_body->value() == 8)
            al_star->activate();
        else
            al_star->deactivate();
        if (horizon->value() == 0) {
            height->activate();
            units->activate();
        } else {
            height->deactivate();
            units->deactivate();
        }
        for (auto p : {course, speed, set, drift})
            if (motion->value())
                p->activate();
            else
                p->deactivate();
        if (fix_at->value()) {
            fix_date->activate();
            fix_time->activate();
        } else {
            fix_date->deactivate();
            fix_time->deactivate();
        }
    }
    void coordinates(Fl_Input *la, Fl_Choice *lah, Fl_Input *lo, Fl_Choice *loh, Sight &s) {
        s.dr_lat = angle(la->value(), "Latitude", 90) * (lah->value() ? -1 : 1);
        s.dr_lon = angle(lo->value(), "Longitude", 180) * (loh->value() ? -1 : 1);
    }
    Sight read_sight() {
        if (body->value() < 0 || body->value() > 7)
            throw std::runtime_error("Choose an observed body.");
        Sight s{};
        date_time(date->value(), time->value(), s);
        const char *b[] = {"sun", "moon", "mercury", "venus", "mars", "jupiter", "saturn", "star"};
        std::snprintf(s.body, sizeof s.body, "%s", b[body->value()]);
        if (body->value() == 7)
            std::snprintf(s.star, sizeof s.star, "%s", nav_star_name(star->value()));
        s.artificial = horizon->value();
        s.hs_deg = angle(hs->value(), "Sextant altitude", s.artificial ? 180 : 90);
        s.ie_min = number(ie->value(), "Index error", 0, 120);
        s.ie_on_arc = arc->value();
        s.he_metres = units->value();
        s.he = s.artificial ? 0 : number(height->value(), "Height of eye", 0, 10000);
        s.limb = 1 - limb->value();
        s.temp_c = number(temp->value(), "Temperature", -100, 100);
        s.press_mb = number(press->value(), "Pressure", 0, 1200);
        coordinates(lat, lat_hemi, lon, lon_hemi, s);
        s.has_motion = motion->value();
        if (s.has_motion) {
            s.course = number(course->value(), "Course", 0, 360);
            s.speed = number(speed->value(), "Speed", 0, 100);
            s.set = number(set->value(), "Set", 0, 360);
            s.drift = number(drift->value(), "Drift", 0, 100);
        }
        std::string name = label->value();
        if (name.find_first_of("\"\r\n") != std::string::npos)
            throw std::runtime_error("Sight labels cannot contain quotes or line breaks.");
        std::snprintf(s.label, sizeof s.label, "%s", name.c_str());
        s.id = edit_id;
        return s;
    }
    void fill_sight(const Sight &s) {
        int index = navsight_body_index(s.body);
        if (std::strcmp(s.body, "star") && (index < 0 || index > 6))
            throw std::runtime_error("Saved sight has an unknown body.");
        edit_id = s.id;
        body->value(!std::strcmp(s.body, "star") ? 7 : navsight_body_index(s.body));
        int idx = nav_star_find(s.star);
        if (idx >= 0)
            star->value(idx);
        char b[64];
        std::snprintf(b, sizeof b, "%04d-%02d-%02d", s.y, s.mo, s.d);
        date->value(b);
        std::snprintf(b, sizeof b, "%02d:%02d:%06.3f", s.h, s.mi, s.s);
        time->value(b);
        hs->value(angle_entry(s.hs_deg, 2).c_str());
        label->value(s.label);
        lat->value(angle_entry(s.dr_lat, 2).c_str());
        lon->value(angle_entry(s.dr_lon, 3).c_str());
        lat_hemi->value(s.dr_lat < 0);
        lon_hemi->value(s.dr_lon < 0);
        horizon->value(s.artificial);
        limb->value(1 - s.limb);
        ie->value(fmt(s.ie_min).c_str());
        arc->value(s.ie_on_arc);
        height->value(fmt(s.he).c_str());
        units->value(s.he_metres);
        temp->value(fmt(s.temp_c).c_str());
        press->value(fmt(s.press_mb).c_str());
        motion->value(s.has_motion);
        course->value(fmt(s.course).c_str());
        speed->value(fmt(s.speed).c_str());
        set->value(fmt(s.set).c_str());
        drift->value(fmt(s.drift).c_str());
        body_state();
        if (edit_id)
            update_button->activate();
        else
            update_button->deactivate();
        report_buffer.text("");
        headline->copy_label(edit_id ? ("Editing sight #" + std::to_string(edit_id)).c_str()
                                     : "New observation");
    }
    void new_sight() {
        Sight s{};
        NavDefaults d;
        navrc_load(&d);
        navrc_apply(&d, &s);
        std::strcpy(s.body, "sun");
        std::time_t t = std::time(nullptr);
        auto g = *std::gmtime(&t);
        s.y = g.tm_year + 1900;
        s.mo = g.tm_mon + 1;
        s.d = g.tm_mday;
        s.h = g.tm_hour;
        s.mi = g.tm_min;
        s.s = g.tm_sec;
        fill_sight(s);
    }
    void example() {
        Sight s{};
        s.y = 2026;
        s.mo = 9;
        s.d = 19;
        s.h = 21;
        s.mi = 38;
        s.s = 5;
        std::strcpy(s.body, "sun");
        s.hs_deg = 94 + 37.2 / 60;
        s.artificial = 1;
        s.limb = 1;
        s.temp_c = 30;
        s.press_mb = 1013;
        s.dr_lat = 34 + 2.5 / 60;
        s.dr_lon = -(117 + 21.9 / 60);
        std::strcpy(s.label, "README Sun example");
        fill_sight(s);
        reduce_sight();
    }
    void show_reduction(const Sight &s, const Reduction &r) {
        report_buffer.text(
            capture([&](FILE *p) {
                std::fprintf(p, "NAVALM %s / %s\nUTC %04d-%02d-%02d %02d:%02d:%06.3f\n\n",
                             NAVALM_VERSION, nav_engine_name(), s.y, s.mo, s.d, s.h, s.mi, s.s);
                navsight_print_correction(p, &s, &r);
                navsight_print_reduction(p, &s, &r);
            }).c_str());
        std::string title = fmt(std::fabs(r.intercept_nm), 1) + " NM " +
                            (r.intercept_nm >= 0 ? "Toward" : "Away") + "    /    Zn " +
                            fmt(r.zn, 1) + " true";
        headline->copy_label(title.c_str());
    }
    void reduce_sight() {
        auto s = read_sight();
        auto r = reduce(s);
        show_reduction(s, r);
        message("Sight reduced with " + std::string(nav_engine_name()) +
                ". Check the worked report before saving.");
    }
    void clear_fix() {
        plot->ready = false;
        plot->redraw();
        if (expanded_plot) {
            expanded_plot->ready = false;
            expanded_plot->redraw();
        }
        fix_buffer.text("");
        fix_sights.clear();
        fix_red.clear();
    }
    void save_sight(bool update) {
        auto s = read_sight();
        auto r = reduce(s);
        SightStore candidate{};
        char err[512]{};
        if (navsight_store_load(&candidate, err, sizeof err))
            throw std::runtime_error(err);
        if (update) {
            Sight *old = navsight_store_find(&candidate, edit_id);
            if (!old) {
                navsight_store_free(&candidate);
                throw std::runtime_error("Select a saved sight to edit first.");
            }
            *old = s;
        } else {
            s.id = 0;
            int id = navsight_store_add(&candidate, &s);
            if (candidate.n == 0 || candidate.v[candidate.n - 1].id != id) {
                navsight_store_free(&candidate);
                throw std::runtime_error("Cannot allocate sight.");
            }
            s.id = id;
        }
        if (navsight_store_save(&candidate, err, sizeof err)) {
            navsight_store_free(&candidate);
            throw std::runtime_error(err);
        }
        navsight_store_free(&store);
        store = candidate;
        edit_id = s.id;
        update_button->activate();
        refresh();
        clear_fix();
        show_reduction(s, r);
        message("Saved sight #" + std::to_string(s.id) + " to " + navsight_store_path());
    }
    void refresh() {
        saved->clear();
        for (size_t i = 0; i < store.n; ++i) {
            auto &s = store.v[i];
            char b[220];
            std::snprintf(b, sizeof b, "#%-3d %-10s %02d-%02d %02d:%02d  %s", s.id,
                          !std::strcmp(s.body, "star") ? s.star : s.body, s.mo, s.d, s.h, s.mi,
                          s.label);
            saved->add(b);
        }
    }
    void load_store() {
        SightStore next{};
        char err[512]{};
        if (navsight_store_load(&next, err, sizeof err))
            throw std::runtime_error(err);
        navsight_store_free(&store);
        store = next;
        refresh();
        clear_fix();
        message(std::to_string(store.n) + " saved sights · " + navsight_store_path());
    }
    std::vector<int> selected() {
        std::vector<int> rows;
        for (int i = 1; i <= saved->size(); ++i)
            if (saved->selected(i))
                rows.push_back(i - 1);
        return rows;
    }
    void edit_sight() {
        auto rows = selected();
        if (rows.size() != 1)
            throw std::runtime_error("Select exactly one sight to edit.");
        fill_sight(store.v[rows[0]]);
        tabs->value(sight_tab);
        reduce_sight();
    }
    void delete_sights() {
        auto rows = selected();
        if (rows.empty())
            throw std::runtime_error("Select one or more sights to delete.");
        if (fl_choice("Delete %lu selected observations from this log?", "Cancel", "Delete",
                      nullptr, (unsigned long)rows.size()) != 1)
            return;
        SightStore next{};
        char err[512]{};
        navsight_store_load(&next, err, sizeof err);
        for (int row : rows)
            navsight_store_delete(&next, store.v[row].id);
        if (navsight_store_save(&next, err, sizeof err)) {
            navsight_store_free(&next);
            throw std::runtime_error(err);
        }
        navsight_store_free(&store);
        store = next;
        refresh();
        clear_fix();
        message("Deleted selected sights.");
    }
    void save_defaults() {
        auto s = read_sight();
        NavDefaults d;
        navrc_init(&d);
        navrc_from_sight(&d, &s);
        char err[512]{};
        if (navrc_save(&d, err, sizeof err))
            throw std::runtime_error(err);
        message("Saved input defaults to " + std::string(navrc_path()));
    }
    void almanac(bool hourly) {
        Sight s{};
        date_time(al_date->value(), al_time->value(), s);
        coordinates(al_lat, al_lat_hemi, al_lon, al_lon_hemi, s);
        check_engine(s);
        int b = al_body->value();
        const char *names[] = {"sun",     "moon",   "mercury", "venus", "mars",
                               "jupiter", "saturn", "aries",   "star"};
        std::strcpy(s.body, names[b]);
        if (b == 8)
            std::snprintf(s.star, sizeof s.star, "%s", nav_star_name(al_star->value()));
        std::string text = capture([&](FILE *p) {
            std::fprintf(p, "NAVALM %s / %s\n%s\n\n", NAVALM_VERSION, nav_engine_name(),
                         nav_engine_detail());
            NavPaper plan{};
            char err[512]{};
            if (navsight_paper(&s, &plan, err, sizeof err))
                throw std::runtime_error(err);
            navsight_print_paper(p, &plan, 108);
            if (hourly) {
                std::fprintf(p,
                             "\nHOURLY ALMANAC · %04d-%02d-%02d UTC\n\n UTC      GHA               "
                             "   Declination           SD         HP\n",
                             s.y, s.mo, s.d);
                for (int h = 0; h < 24; ++h) {
                    double jd = nav_julian_date(s.y, s.mo, s.d, h, 0, 0);
                    if (!nav_engine_covers(jd))
                        throw std::runtime_error(
                            "Ephemeris does not cover the entire requested day.");
                    double gha, dec, sd = 0, hp = 0;
                    if (b == 8) {
                        NavStarAlmanac a{};
                        if (nav_star_almanac(al_star->value(), jd, &a))
                            throw std::runtime_error(nav_engine_last_error());
                        gha = a.gha_deg;
                        dec = a.dec_deg;
                    } else {
                        NavAlmanac a{};
                        if (nav_almanac((NavBody)b, jd, &a))
                            throw std::runtime_error(nav_engine_last_error());
                        gha = a.gha_deg;
                        dec = a.dec_deg;
                        sd = a.sd_deg * 60;
                        hp = a.hp_deg * 60;
                    }
                    char g[64], d[64];
                    std::fprintf(p, " %02d:00    %-20s %-20s %7.2f'   %7.2f'\n", h,
                                 navsight_fmt_gha2(gha, g, sizeof g),
                                 navsight_fmt_lat2(dec, d, sizeof d), sd, hp);
                }
            }
        });
        al_buffer.text(text.c_str());
        message(hourly ? "Hourly table calculated in UTC."
                       : "Paper almanac calculated at the exact DR.");
    }
    void calculate_fix() {
        clear_fix();
        auto rows = selected();
        if (rows.size() < 2 || rows.size() > 16)
            throw std::runtime_error("Select between 2 and 16 saved sights.");
        std::vector<Sight> s;
        std::vector<Reduction> r;
        double lo = 180, hi = -180;
        for (int i : rows) {
            s.push_back(store.v[i]);
            r.push_back(reduce(s.back()));
            lo = std::min(lo, r.back().ap_lon);
            hi = std::max(hi, r.back().ap_lon);
        }
        if (hi - lo > 180)
            throw std::runtime_error("This local-plane fix cannot span the antimeridian.");
        Sight at{};
        if (fix_at->value())
            date_time(fix_date->value(), fix_time->value(), at);
        FixResult f{};
        char err[512]{};
        if (navsight_fix(s.data(), r.data(), s.size(), fix_at->value(), at.y, at.mo, at.d, at.h,
                         at.mi, at.s, &f, err, sizeof err))
            throw std::runtime_error(err);
        if (!std::isfinite(f.lat) || !std::isfinite(f.lon) || std::fabs(f.lat) >= 89.9 ||
            std::fabs(f.lon) > 180)
            throw std::runtime_error("Fix is outside this regional plotting view.");
        fix_result = f;
        fix_sights = s;
        fix_red = r;
        plot->scene = overlay(s, r, f);
        plot->ready = true;
        plot->fit_view();
        if (expanded_plot) {
            expanded_plot->scene = plot->scene;
            expanded_plot->ready = true;
            expanded_plot->fit_view();
        }
        fix_buffer.text(capture([&](FILE *p) {
                            char a[64], b[64];
                            std::fprintf(
                                p, "FIX %s  %s\nUTC %04d-%02d-%02d %02d:%02d:%06.3f\nRMS %.3f NM\n",
                                navsight_fmt_lat(f.lat, a, sizeof a),
                                navsight_fmt_lon(f.lon, b, sizeof b), f.fy, f.fmo, f.fd, f.fh,
                                f.fmi, f.fs, f.rms_nm);
                            if (f.have_ellipse)
                                std::fprintf(p, "1-sigma ellipse %.3f x %.3f NM / %.1f T\n",
                                             f.semi_major_nm, f.semi_minor_nm, f.ellipse_brg);
                            for (size_t i = 0; i < s.size(); ++i)
                                std::fprintf(p, "#%d residual %+.3f NM; advance %.3f NM\n", s[i].id,
                                             f.residual_nm[i], f.advanced_nm[i]);
                        }).c_str());
        message("Fix calculated · " + std::to_string(s.size()) +
                " LOPs · plotting vectors anchored to latitude / longitude.");
    }
    void update_map_views() {
        for (Plot *target : {plot, expanded_plot})
            if (target) {
                target->land_map = land_map;
                target->map_visible = map_toggle->value();
                target->cached_map = nullptr;
                target->redraw();
            }
        std::string info = land_map ? land_map->name + "\n" + land_map->path
                                    : "No map installed. Download the world map, load a .shp, or "
                                      "use your OpenCPN coastline.";
        if (map_information)
            map_information->copy_label(info.c_str());
    }
    void save_map_setting() {
        if (testing || !land_map)
            return;
        auto path = fs::u8path(land_map->path);
        auto relative = path.lexically_relative(application_root);
        std::string stored = !relative.empty() && *relative.begin() != ".."
                                 ? relative.generic_u8string()
                                 : path.u8string();
        write_file((application_root / ".local/basemap.path").u8string(),
                   std::to_string(map_toggle->value()) + "\n" + stored + "\n");
    }
    void set_land_map(std::shared_ptr<LandMap> map, bool persist = true) {
        land_map = std::move(map);
        map_toggle->value(1);
        update_map_views();
        if (persist)
            save_map_setting();
        message("Offline coastline map loaded. Calculate a fix to see it beneath the UGP.");
    }
    void load_map_setting() {
        if (const char *override = std::getenv("NAVALM_BASEMAP")) {
            try {
                set_land_map(load_land_map(override), false);
            } catch (const std::exception &e) {
                message(e.what());
            }
            return;
        }
        std::ifstream setting(application_root / ".local/basemap.path");
        std::string enabled, saved_path;
        std::getline(setting, enabled);
        std::getline(setting, saved_path);
        if (!saved_path.empty()) {
            auto p = fs::u8path(saved_path);
            if (p.is_relative())
                p = application_root / p;
            try {
                set_land_map(load_land_map(p.u8string()), false);
                map_toggle->value(enabled != "0");
                update_map_views();
                return;
            } catch (const std::exception &) {
            }
        }
        for (const auto &p : {application_root / ".local/maps/ne_10m_land.shp",
                              application_root / "maps/ne_10m_land.shp",
                              application_resources / "maps/ne_10m_land.shp"})
            if (fs::exists(p)) {
                try {
                    set_land_map(load_land_map(p.u8string()), false);
                    return;
                } catch (const std::exception &) {
                }
            }
        update_map_views();
    }
    void use_opencpn_map() {
        const char *home = std::getenv("HOME");
        const char *appdata = std::getenv("APPDATA");
        std::vector<fs::path> configs;
        if (home) {
            configs.push_back(fs::u8path(home) / "Library/Preferences/opencpn/opencpn.ini");
            configs.push_back(fs::u8path(home) / ".opencpn/opencpn.conf");
        }
        if (appdata)
            configs.push_back(fs::u8path(appdata) / "opencpn/opencpn.ini");
        for (const auto &ini : configs)
            for (const auto &p : opencpn_map_paths(ini.u8string()))
                if (fs::exists(fs::u8path(p))) {
                    try {
                        auto map = load_land_map(p);
                        map->name = "OpenCPN OSM coastline — offline";
                        set_land_map(map);
                        return;
                    } catch (const std::exception &) {
                    }
                }
        throw std::runtime_error("No compatible OpenCPN medium/low coastline found. Use Load land "
                                 "map or Download world map.");
    }
    static void map_download_tick(void *p) {
        auto *app = static_cast<Workbench *>(p);
        try {
            app->poll_map_download();
        } catch (const std::exception &e) {
            app->message(e.what(), true);
        }
    }
    void start_map_download() {
        if (map_downloading)
            return;
        auto dir = testing ? fs::u8path(std::getenv("NAVALM_SAVE")).parent_path() / "maps"
                           : application_root / ".local/maps";
        fs::create_directories(dir);
        auto target = dir / "ne_10m_land.shp",
             part = dir /
                    ("ne_10m_land-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ".part");
        map_cancel = false;
        map_done = false;
        map_bytes = 0;
        map_download_error.clear();
        downloaded_map.reset();
        map_downloading = true;
        download_map_button->deactivate();
        cancel_map_button->activate();
        message("Downloading Natural Earth world coastline…");
        map_worker = std::thread([this, target, part] {
            try {
                download_https(world_map_url, part, map_cancel, map_bytes);
                auto result = load_land_map(part.u8string());
                if (map_cancel)
                    throw std::runtime_error("Download cancelled.");
                // Replace only after complete download and structural validation.
                if (nav_replace_file(part.u8string().c_str(), target.u8string().c_str()))
                    throw std::runtime_error("Cannot install downloaded map.");
                result->path = target.u8string();
                result->name = "Natural Earth 1:10 million — land and coastline";
                downloaded_map = std::move(result);
            } catch (const std::exception &e) {
                map_download_error = e.what();
                std::error_code ignored;
                fs::remove(part, ignored);
            }
            map_done.store(true, std::memory_order_release);
        });
        Fl::add_timeout(.1, map_download_tick, this);
    }
    void poll_map_download() {
        if (!map_downloading)
            return;
        if (!map_done.load(std::memory_order_acquire)) {
            std::string progress =
                "Downloading Natural Earth: " + fmt(map_bytes.load() / 1048576., 1) +
                " MB — the workbench remains available.";
            map_information->copy_label(progress.c_str());
            Fl::repeat_timeout(.1, map_download_tick, this);
            return;
        }
        if (map_worker.joinable())
            map_worker.join();
        map_downloading = false;
        download_map_button->activate();
        cancel_map_button->deactivate();
        if (downloaded_map) {
            set_land_map(downloaded_map);
            message("World map installed — available offline beneath the plotting vectors.");
        } else {
            update_map_views();
            message(map_download_error, !map_cancel);
        }
    }
    Plot *active_plot() {
        return expanded_window && expanded_window->shown() ? expanded_plot : plot;
    }
    void update_zoom_readout() {
        if (!zoom_readout)
            return;
        std::string text = std::to_string((int)std::round(active_plot()->zoom * 100)) + "%";
        zoom_readout->copy_label(text.c_str());
        if (expanded_zoom_readout)
            expanded_zoom_readout->copy_label(text.c_str());
    }
    void reattach_plot() {
        if (expanded_window)
            expanded_window->hide();
        plot->show();
        detached_hint->hide();
        detach_button->copy_label("Detach sheet…");
        if (expanded_plot) {
            double base =
                std::min((plot->w() - 16) / Sheet::width, (plot->h() - 16) / Sheet::height);
            double from = std::min((expanded_plot->w() - 16) / Sheet::width,
                                   (expanded_plot->h() - 16) / Sheet::height);
            plot->zoom = expanded_plot->zoom;
            plot->pan_x = expanded_plot->pan_x * base / from;
            plot->pan_y = expanded_plot->pan_y * base / from;
        }
        plot->changed();
    }
    void expand_plot() {
        if (!plot->ready)
            throw std::runtime_error("Calculate a fix before detaching the sheet.");
        if (!expanded_window) {
            expanded_window =
                std::make_unique<Fl_Double_Window>(800, 830, "NAVALM — Universal Plotting Sheet");
            expanded_window->begin();
            button(12, 8, 44, "−", [this] { expanded_plot->zoom_by(.8); });
            button(64, 8, 64, "Fit", [this] { expanded_plot->fit_view(); });
            button(136, 8, 44, "+", [this] { expanded_plot->zoom_by(1.25); });
            expanded_zoom_readout = new Fl_Box(188, 8, 80, 34, "100%");
            auto *help = new Fl_Box(280, 8, 330, 34, "Wheel zoom · drag pan · double-click fit");
            help->labelsize(12);
            button(636, 8, 152, "Reattach sheet", [this] { reattach_plot(); });
            expanded_plot = new Plot(0, 50, 800, 780);
            expanded_plot->view_changed = [this] { update_zoom_readout(); };
            expanded_plot->tooltip(
                "Wheel: zoom at pointer. Drag: pan. Double-click / 0: fit. + / -: zoom.");
            expanded_window->resizable(expanded_plot);
            expanded_window->size_range(800, 520);
            expanded_window->end();
            expanded_window->callback(
                [](Fl_Widget *, void *p) { static_cast<Workbench *>(p)->reattach_plot(); }, this);
        }
        expanded_plot->scene = plot->scene;
        expanded_plot->ready = true;
        double base = std::min((plot->w() - 16) / Sheet::width, (plot->h() - 16) / Sheet::height);
        double to = std::min((expanded_plot->w() - 16) / Sheet::width,
                             (expanded_plot->h() - 16) / Sheet::height);
        expanded_plot->zoom = plot->zoom;
        expanded_plot->pan_x = plot->pan_x * to / base;
        expanded_plot->pan_y = plot->pan_y * to / base;
        plot->hide();
        detached_hint->show();
        detach_button->copy_label("Reattach sheet");
        update_layers();
        update_map_views();
        expanded_window->show();
        expanded_plot->take_focus();
        update_zoom_readout();
    }
    void update_layers() {
        for (Plot *target : {plot, expanded_plot}) {
            if (!target)
                continue;
            for (int i = 0; i < 4; ++i)
                target->layer_visible[i] = layers[i]->value();
            target->opacity = opacity->value();
            target->redraw();
        }
    }
    void export_text(Fl_Text_Buffer &b, const char *name) {
        if (b.length() == 0)
            throw std::runtime_error("Calculate a report first.");
        auto path = choose_file("Export NAVALM report", "Text\t*.txt", true, name);
        if (path.empty())
            return;
        char *s = b.text();
        std::string data(s);
        std::free(s);
        write_file(path, data);
        message("Exported " + path);
    }
    void export_overlay(bool geo) {
        if (!plot->ready)
            throw std::runtime_error("Calculate a fix before exporting vectors.");
        auto path = choose_file(geo ? "Export geographic vector paths"
                                    : "Export transparent plotting overlay",
                                geo ? "GeoJSON\t*.geojson" : "SVG\t*.svg", true,
                                geo ? "navalm-overlay.geojson" : "navalm-overlay.svg");
        if (path.empty())
            return;
        write_file(path, geo ? geojson(plot->scene)
                             : svg(plot->scene, plot->layer_visible, plot->opacity));
        message("Exported " + path);
    }
    void engine_info() {
        engine_status->copy_label(nav_engine_detail());
        std::string s =
            "Saved sights: " + std::string(navsight_store_path()) + "\nDefaults: " + navrc_path();
        paths->copy_label(s.c_str());
    }
    void change_engine() {
        int previous = nav_engine_current() == NAV_ENGINE_PRECISE ? 0 : 1;
        nav_engine_select(engine->value() ? "classic" : "precise");
        char err[512]{};
        if (nav_engine_init(err, sizeof err)) {
            nav_engine_select(previous ? "classic" : "precise");
            nav_engine_init(nullptr, 0);
            engine->value(previous);
            throw std::runtime_error(err);
        }
        clear_fix();
        report_buffer.text("");
        headline->copy_label("Engine changed — reduce the sight again.");
        al_buffer.text("");
        engine_info();
        message("Using " + std::string(nav_engine_name()));
    }
    void choose_ephemeris() {
        auto path = choose_file("Select repacked JPL ephemeris", "NAVALM ephemeris\t*.nav", false);
        if (path.empty())
            return;
        nav_engine_shutdown();
        nav_engine_set_ephemeris_path(path.c_str());
        nav_engine_select("precise");
        char err[512]{};
        if (nav_engine_init(err, sizeof err)) {
            nav_engine_select("classic");
            nav_engine_init(nullptr, 0);
            engine->value(1);
            clear_fix();
            report_buffer.text("");
            al_buffer.text("");
            engine_info();
            headline->copy_label("Ephemeris unavailable — using Classic. Reduce again.");
            throw std::runtime_error(err);
        }
        engine->value(0);
        clear_fix();
        report_buffer.text("");
        al_buffer.text("");
        headline->copy_label("Ephemeris changed — reduce the sight again.");
        engine_info();
        message("Loaded " + path);
    }
};
static fs::path runtime_root(const char *arg) {
    fs::path executable = fs::absolute(fs::u8path(arg));
#ifdef __APPLE__
    char path[4096];
    uint32_t n = sizeof path;
    if (_NSGetExecutablePath(path, &n) == 0)
        executable = fs::weakly_canonical(path);
#elif defined(_WIN32)
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (n && n < 32768)
        executable = fs::path(std::wstring(path, n));
#endif
    auto root = executable.parent_path();
    if (root.filename() == "MacOS" && root.parent_path().filename() == "Contents")
        return root.parent_path().parent_path().parent_path();
    return root;
}
static Overlay checked_scene;
static std::shared_ptr<LandMap> checked_map;
static int self_test();
int main(int argc, char **argv) {
    bool checks = std::string(argv[0]).find("navalm-checks") != std::string::npos;
    try {
        auto root = runtime_root(argv[0]);
        application_root = root;
        application_resources = fs::absolute(fs::u8path(argv[0])).parent_path() / "../Resources";
        fs::create_directories(root / ".local");
#ifdef NAVALM_EMBEDDED_DATA
        nav_unpack_resource(901, root / ".local/ephem/de440s.nav");
        nav_unpack_resource(902, root / ".local/maps/ne_10m_land.shp");
#endif
        if (checks) {
            fs::current_path(root);
            fs::create_directories(root / ".build/desktop-test");
            std::freopen((root / ".build/desktop-test/ui-checks.txt").u8string().c_str(), "w",
                         stdout);
            std::freopen((root / ".build/desktop-test/ui-errors.txt").u8string().c_str(), "w",
                         stderr);
            setvbuf(stdout, nullptr, _IONBF, 0);
        }
        if (!std::getenv("NAVALM_SAVE"))
            nav_setenv("NAVALM_SAVE", (root / ".local/desktop-sights.save").u8string().c_str(), 0);
        if (!std::getenv("NAVALMRC"))
            nav_setenv("NAVALMRC", (root / ".local/desktop-defaults.rc").u8string().c_str(), 0);
        if (!std::getenv("NAVALM_EPHEMERIS")) {
            auto ep = root / "ephem/de440s.nav";
            if (!fs::exists(ep))
                ep = root / ".local/ephem/de440s.nav";
            auto bundle = fs::path(argv[0]).parent_path() / "../Resources/de440s.nav";
            if (fs::exists(ep))
                nav_engine_set_ephemeris_path(ep.u8string().c_str());
            else if (fs::exists(bundle))
                nav_engine_set_ephemeris_path(bundle.u8string().c_str());
        }
        nav_engine_select("precise");
        char err[512]{};
        std::string startup;
        if (nav_engine_init(err, sizeof err)) {
            startup = std::string("Precise unavailable: ") + err + " Using Classic.";
            nav_engine_select("classic");
            nav_engine_init(nullptr, 0);
        }
        if (checks || (argc > 1 && !std::strcmp(argv[1], "--self-test"))) {
            if (checks)
                nav_setenv("NAVALM_TEST_DOWNLOAD", "1", 0);
            int code = self_test();
            nav_engine_shutdown();
            if (checks) {
                Fl_Double_Window result(800, 870,
                                        "NAVALM desktop checks passed — synthetic coastal fix");
                Fl_Box text(16, 8, 768, 40,
                            "All desktop checks passed · synthetic coastal fix preview\n"
                            "Report: .build/desktop-test/ui-checks.txt");
                text.labelsize(13);
                Plot preview(0, 50, 800, 820);
                preview.scene = checked_scene;
                preview.land_map = checked_map;
                preview.ready = true;
                result.resizable(preview);
                result.end();
                result.show();
                preview.take_focus();
                Fl::run();
            }
            return code;
        }
        Workbench app;
        if (!startup.empty())
            app.message(startup);
        app.win.show();
        int code = Fl::run();
        nav_engine_shutdown();
        return code;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "NAVALM desktop: %s\n", e.what());
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "NAVALM could not start", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}

static int self_test() {
    auto require = [](bool v, const char *s) {
        if (!v)
            throw std::runtime_error(std::string("SELF TEST: ") + s);
    };
    auto rejects = [&](std::function<void()> f) {
        try {
            f();
        } catch (const std::runtime_error &) {
            return;
        }
        require(false, "invalid input was accepted");
    };
    fs::path testdir = nav_temp_dir();
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(path, e);
        }
    } cleanup{testdir};
    auto log = (testdir / "sights.save").string(), rc = (testdir / "defaults.rc").string();
    nav_setenv("NAVALM_SAVE", log.c_str(), 1);
    nav_setenv("NAVALMRC", rc.c_str(), 1);
    require(std::fabs(angle("94 37.2", "Hs", 180) - (94 + 37.2 / 60)) < 1e-10,
            "degrees/minutes parse");
    for (const char *s : {"", "nan", "inf", "1x", "-1", "91"})
        rejects([&] { angle(s, "Latitude", 90); });
    for (const char *s : {"34 60", "34.5 12", "34 1 junk"})
        rejects([&] { angle(s, "Latitude", 90); });
    rejects([&] { number("nan", "value", 0, 1); });
    rejects([&] { number("", "value", 0, 1); });
    Sight date{};
    date_time("2024-02-29", "23:59:59.5", date);
    for (const char *s : {"2026-02-29", "2026-04-31", "2026-13-01", "2026-01-00", "2026-10-18x"})
        rejects([&] { date_time(s, "12:00:00", date); });
    for (const char *s : {"24:00:00", "12:60:00", "12:00:nan", "12:00:60", "12:00:00x"})
        rejects([&] { date_time("2026-10-18", s, date); });
    std::puts("PASS input validation: nonfinite, ranges, calendar, fractional seconds");
    Workbench app;
    app.testing = true;
    app.win.show();
    Fl::check();
    app.example();
    require(std::string(app.hs->value()) == "94 37.2" &&
                std::string(app.lat->value()) == "34 02.5" &&
                std::string(app.lon->value()) == "117 21.9",
            "example displays degrees/minutes");
    auto s = app.read_sight();
    app.lat->value("34 02.5");
    app.lon->value("117 21.9");
    app.hs->value("94 37.2");
    auto spaced = app.read_sight();
    require(std::fabs(spaced.dr_lat - (34 + 2.5 / 60)) < 1e-10 &&
                std::fabs(spaced.dr_lon + (117 + 21.9 / 60)) < 1e-10 &&
                std::fabs(spaced.hs_deg - (94 + 37.2 / 60)) < 1e-10,
            "DR and Hs fields accept a space between degrees and minutes");
    app.lat->value("34.0416666666667");
    app.lon->value("117.365");
    app.hs->value("94.62");
    auto decimal_input = app.read_sight();
    require(std::fabs(decimal_input.dr_lat - spaced.dr_lat) < 1e-10 &&
                std::fabs(decimal_input.dr_lon - spaced.dr_lon) < 1e-10 &&
                std::fabs(decimal_input.hs_deg - spaced.hs_deg) < 1e-10,
            "decimal degrees and spaced degrees/minutes produce identical inputs");
    app.fill_sight(decimal_input);
    require(std::string(app.lat->value()) == "34 02.5" &&
                std::string(app.lon->value()) == "117 21.9",
            "loaded DR stays in degrees/minutes");
    auto r = reduce(s);
    require(std::fabs(r.intercept_nm - 6.8) < .06, "README Sun intercept");
    app.save_button->do_callback();
    require(app.store.n == 1, "Save as new");
    int id = app.edit_id;
    app.label->value("Edited Sun example");
    app.update_button->do_callback();
    require(app.store.n == 1 && app.store.v[0].id == id,
            "update must preserve ID without adding row");
    require(std::string(app.store.v[0].label) == "Edited Sun example", "update must persist label");
    app.save_button->do_callback();
    require(app.store.n == 2 && app.store.v[1].id != id, "save new assigns distinct ID");
    app.load_store();
    require(app.store.n == 2, "reload store");
    app.saved->select(1);
    app.edit_sight();
    require(app.edit_id == id, "edit selected observation");
    app.hs->value("nan");
    rejects([&] { app.read_sight(); });
    app.example();
    app.motion->value(0);
    app.save_defaults();
    NavDefaults defaults;
    navrc_load(&defaults);
    require(!defaults.have_motion, "defaults without motion");
    app.al_date->value("2026-09-19");
    app.al_time->value("21:38:05");
    app.al_lat->value("34 2.5");
    app.al_lon->value("117 21.9");
    app.al_lon_hemi->value(1);
    app.almanac(true);
    char *text = app.al_buffer.text();
    std::string report(text);
    std::free(text);
    require(report.find("23:00") != std::string::npos, "hourly almanac includes whole day");
    app.al_body->value(8);
    app.almanac(false);
    std::puts("PASS native callbacks: Sun reduction, add, edit/update, reload, defaults, "
              "star/hourly almanac, spaced DR/Hs and display roundtrip");
    app.engine->value(1);
    app.change_engine();
    require(nav_engine_current() == NAV_ENGINE_CLASSIC, "engine selector Classic");
    app.example();
    app.engine->value(0);
    app.change_engine();
    require(nav_engine_current() == NAV_ENGINE_PRECISE, "engine selector precise");
    fs::path output = fs::current_path() / ".build/desktop-test";
    fs::create_directories(output);
    for (const char *name : {"stationary", "running"}) {
        fs::copy_file(fs::path("tests/fixtures") / (std::string("desktop-") + name + ".save"), log,
                      fs::copy_options::overwrite_existing);
        app.load_store();
        for (int i = 1; i <= app.saved->size(); ++i)
            app.saved->select(i);
        app.fix_at->value(0);
        app.calculate_fix();
        require(app.plot->ready, "fix plot readiness");
        auto f = app.fix_result;
        auto &o = app.plot->scene;
        checked_scene = o;
        auto lines = geometry(app.fix_red, f);
        for (size_t i = 0; i < lines.size(); ++i) {
            auto &l = lines[i];
            require(std::fabs(-(l.e * l.ue + l.n * l.un) - f.residual_nm[i]) < 1e-6,
                    "plot signed distance must equal solver residual");
            auto q = o.geographic(l.e, l.n);
            auto v = o.plane(q);
            require(std::fabs(v.first - l.e) < 1e-8 && std::fabs(v.second - l.n) < 1e-8,
                    "geographic vector round trip");
        }
        bool visible[] = {true, true, true, true};
        auto svgtext = svg(o, visible, .55);
        require(svgtext.find("opacity=\"0.55\"") != std::string::npos, "SVG opacity");
        require(svgtext.find("<rect x=\"46\" y=\"46\" width=\"668\" height=\"496\"/>") !=
                    std::string::npos,
                "clip rectangle");
        require(svgtext.find("<rect fill=") == std::string::npos, "SVG has no painted background");
        auto vectors = geojson(o);
        require(vectors.find("FeatureCollection") != std::string::npos,
                "GeoJSON geographic vectors");
        write_file((output / (std::string(name) + ".svg")).string(), svgtext);
        write_file((output / (std::string(name) + ".geojson")).string(), vectors);
        app.expand_plot();
        Fl::check();
        require(app.expanded_window->shown() && app.expanded_plot->ready && !app.plot->visible(),
                "detached sheet opens and leaves the dock");
        auto *detached = app.expanded_plot;
        detached->fit_view();
        double anchor_x = detached->x() + 210, anchor_y = detached->y() + 180;
        double before_x = anchor_x - (detached->x() + detached->w() / 2.);
        double before_y = anchor_y - (detached->y() + detached->h() / 2.);
        detached->zoom_at(2, anchor_x, anchor_y);
        require(std::fabs((before_x - detached->pan_x) / detached->zoom - before_x) < 1e-9 &&
                    std::fabs((before_y - detached->pan_y) / detached->zoom - before_y) < 1e-9,
                "wheel zoom preserves the point beneath the pointer");
        require(std::string(app.zoom_readout->label()) == "200%", "zoom readout");
        detached->zoom_by(100);
        require(detached->zoom == 8, "zoom upper bound");
        detached->zoom_by(.0001);
        require(detached->zoom == .25, "zoom lower bound");
        detached->fit_view();
        require(detached->zoom == 1 && detached->pan_x == 0 && detached->pan_y == 0,
                "Fit resets zoom and pan");
        app.expanded_window->do_callback();
        require(!app.expanded_window->shown() && app.plot->visible(),
                "closing detached window reattaches sheet");
        app.expand_plot();
        require(app.expanded_plot == detached && app.expanded_window->shown(),
                "detach reuses the linked window");
        app.layers[0]->value(0);
        app.opacity->value(.55);
        app.update_layers();
        require(!app.plot->layer_visible[0] && app.plot->opacity == .55 &&
                    !app.expanded_plot->layer_visible[0] && app.expanded_plot->opacity == .55,
                "layer controls update embedded and expanded sheets");
        app.layers[0]->value(1);
        app.fix_at->value(1);
        app.fix_date->value("2026-09-20");
        app.fix_time->value("04:00:00");
        app.calculate_fix();
        require(app.fix_result.fh == 4 && app.expanded_plot->ready &&
                    app.expanded_plot->scene.fix.fh == 4 && app.expanded_window->shown(),
                "explicit fix UTC updates the detached plot without closing it");
        app.fix_at->value(0);
        std::printf("PASS %s fix: %.8f %.8f; vector residuals, geographic anchors, SVG/GeoJSON, "
                    "layers, detach/reattach, anchored zoom, Fit, explicit UTC\n",
                    name, f.lat, f.lon);
    }
    // Exercise the actual background downloader through the FLTK event loop.
    // Its output and all test navigation state stay in the private test directory.
    require(app.land_map != nullptr, "bundled offline map is available");
    if (std::getenv("NAVALM_TEST_DOWNLOAD")) {
        app.start_map_download();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        while (app.map_downloading && std::chrono::steady_clock::now() < deadline)
            Fl::wait(.05);
        require(!app.map_downloading && app.downloaded_map &&
                    fs::exists(testdir / "maps/ne_10m_land.shp"),
                "background HTTPS download installs a valid offline map");
        auto installed = app.land_map;
        app.start_map_download();
        app.map_cancel = true;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (app.map_downloading && std::chrono::steady_clock::now() < deadline)
            Fl::wait(.05);
        require(!app.map_downloading && !app.downloaded_map && app.land_map == installed,
                "cancelling a download preserves the installed map");
        std::puts("PASS background HTTPS install and cancellation preserves installed map");
    }
    auto installed = app.land_map;
    const char *test_home = std::getenv("HOME");
    if (test_home &&
        fs::exists(fs::u8path(test_home) / "Library/Preferences/opencpn/opencpn.ini")) {
        app.use_opencpn_map();
        require(app.land_map && app.land_map->points > 1000, "read-only OpenCPN coastline import");
        std::puts("PASS read-only import of installed OpenCPN coastline");
    }
    app.set_land_map(installed, false);
    app.map_toggle->value(0);
    app.map_toggle->do_callback();
    require(!app.plot->map_visible && !app.expanded_plot->map_visible,
            "map visibility synchronizes in both views");
    app.map_toggle->value(1);
    app.map_toggle->do_callback();
    require(app.plot->land_map == app.expanded_plot->land_map && app.plot->map_visible,
            "detached view shares the same geographic underlay");
    // Generate a coastal fixture using real reductions, with synthetic sextant
    // altitudes chosen for a small fix near Los Angeles harbour.
    auto coastal = checked_scene.sights;
    std::vector<Reduction> coastal_red;
    for (size_t i = 0; i < coastal.size(); ++i) {
        auto &v = coastal[i];
        v.dr_lat = 33.7;
        v.dr_lon = -118.1;
        v.has_motion = 0;
        for (int k = 0; k < 5; ++k) {
            auto red = reduce(v);
            double desired = (33.7 - red.ap_lat) * 60 * std::cos(red.zn * M_PI / 180) +
                             (-118.1 - red.ap_lon) * 60 * std::cos(red.ap_lat * M_PI / 180) *
                                 std::sin(red.zn * M_PI / 180);
            v.hs_deg += (desired - red.intercept_nm + (i == 1 ? .03 : 0)) / 60;
        }
        coastal_red.push_back(reduce(v));
    }
    FixResult coastal_fix{};
    char coastal_error[512]{};
    require(!navsight_fix(coastal.data(), coastal_red.data(), coastal.size(), 0, 0, 0, 0, 0, 0, 0,
                          &coastal_fix, coastal_error, sizeof coastal_error),
            "synthetic coastal fix calculates");
    require(std::fabs(coastal_fix.lat - 33.7) < .005 && std::fabs(coastal_fix.lon + 118.1) < .005,
            "coastal fixture lands at the requested geographic position");
    checked_scene = overlay(coastal, coastal_red, coastal_fix);
    checked_map = installed;
    require(!map_contours(*installed, checked_scene, make_sheet(checked_scene)).empty(),
            "coastal underlay contains visible land contours");
    std::puts("PASS offline map: linked visibility and synthetic coastal fix");
    app.engine->value(1);
    app.change_engine();
    require(!app.plot->ready && app.fix_buffer.length() == 0, "engine change clears stale fix");
    app.win.hide();
    Fl::check();
    std::puts("All desktop checks passed; no personal navigation files used.");
    return 0;
}
