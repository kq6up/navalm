/* SPDX-License-Identifier: MIT
 * Native FLTK workbench over NAVALM's unchanged C calculation engine.
 */
#include "navplatform.h"

#include "nav_version.h"
#include "navdownload.h"
#include "navbook.h"
#include "navchrono_sight.h"
#include "navfltk_chrono.h"
#include "navfltk_cluster.h"
#include "navfltk_lunar.h"
#include "navfile.h"
#include "navfltk_map.h"
#include "navfltk_plot.h"
#include "navfltk_sky.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Progress.H>
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

// Xlib (included by FLTK 1.3) defines None as a macro, which conflicts
// with navbook::Reduction::None used below. Keep the fix local to this TU.
#ifdef None
#undef None
#endif

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
        if (ready)
            nav_set_desktop_action("Drawing plotting sheet");
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
            fl_pop_matrix();
            double map_left = std::max(double(x()), left + b.x * scale),
                   map_top = std::max(double(y()), top + b.y * scale),
                   map_right = std::min(double(x() + w()), left + (b.x + b.w) * scale),
                   map_bottom = std::min(double(y() + h()), top + (b.y + b.h) * scale);
            Box raster_clip{map_left, map_top, map_right - map_left, map_bottom - map_top};
            fl_color(fl_rgb_color(227, 222, 199));
            for (const auto &polygon : land_cache)
                for (auto span : map_fill_spans(polygon, raster_clip, left, top, scale))
                    fl_rectf(span.x0, span.y, span.x1 - span.x0 + 1, 1);
            fl_push_matrix();
            fl_translate(left, top);
            fl_scale(scale, scale);
            for (const auto &polygon : land_cache) {
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
    Fl_Double_Window win{1180, 850, "NavAlm " NAVALM_VERSION " — Navigation Workbench"};
    Fl_Tabs *tabs;
    Fl_Group *almanac_tab, *sight_tab, *fix_tab, *chart_tab;
    Fl_Box *status, *engine_status, *headline, *paths;
    Fl_Choice *engine, *body, *star, *horizon, *limb, *arc, *units, *lat_hemi, *lon_hemi, *al_body,
        *al_star, *al_lat_hemi, *al_lon_hemi;
    Fl_Input *date, *time, *hs, *label, *lat, *lon, *ie, *height, *temp, *press, *course, *speed,
        *set, *drift;
    Fl_Input *al_date, *al_time, *al_lat, *al_lon, *fix_date, *fix_time, *short_distance;
    Fl_Group *defaults_tab;
    Fl_Input *def_lat,*def_lon,*def_ie,*def_height,*def_temp,*def_press,*def_short,*def_course,*def_speed,*def_set,*def_drift;
    Fl_Choice *def_lat_hemi,*def_lon_hemi,*def_arc,*def_units,*def_horizon,*def_limb;
    Fl_Check_Button *def_at_dr,*def_motion;
    LunarPanel *lunar;
    Fl_Group *chrono_tab;
    Fl_Choice *chrono_clock,*chrono_source,*sight_clock;
    Fl_Input *chrono_name,*chrono_ref_date,*chrono_ref_time,*chrono_watch_date,*chrono_watch_time,*chrono_note;
    Fl_Input *sight_watch_date,*sight_watch_time;
    Fl_Check_Button *chrono_apply_new,*sight_clock_apply;
    Fl_Button *chrono_add_button,*chrono_cancel_button,*chrono_edit_button,*chrono_delete_button,*chrono_exclude_button;
    ChronoPlot *chrono_plot;
    long chrono_edit_index=-1;
    Fl_Hold_Browser *chrono_rows;
    Fl_Box *chrono_summary,*sight_clock_status;
    chrono_nav::Log clock_log;
    std::vector<size_t> chrono_row_ids;
    fs::path chrono_path;
    std::string chrono_error;
    Sight clock_snapshot{};
    Fl_Check_Button *at_dr, *motion, *fix_at, *layers[4], *map_toggle;
    Fl_Value_Slider *opacity;
    Fl_Multi_Browser *saved;
    Plot *plot;
    std::unique_ptr<Fl_Double_Window> expanded_window;
    Plot *expanded_plot = nullptr;
    Fl_Box *detached_hint, *zoom_readout = nullptr, *expanded_zoom_readout = nullptr;
    Fl_Button *detach_button, *download_map_button, *cancel_map_button;
    Fl_Box *map_information;
    Fl_Group *book_tab;
    Fl_Group *sky_tab;
    Fl_Input *sky_date,*sky_time,*sky_lat,*sky_lon,*sky_min,*sky_max,*sky_mag;
    Fl_Choice *sky_lat_hemi,*sky_lon_hemi,*sky_count;
    Fl_Check_Button *sky_names,*sky_below;
    Fl_Hold_Browser *sky_list;
    Fl_Box *sky_status,*sky_summary,*sky_detail;
    Fl_Button *sky_sight;
    Fl_Box *sky_zoom_readout=nullptr, *sky_detached_zoom_readout=nullptr;
    SkyCanvas *sky_canvas;
    std::unique_ptr<SkyWindow> sky_window;
    Fl_Check_Button *sky_layers[3], *sky_detached_layers[3]{};
    Fl_Check_Button *sky_detached_names=nullptr;
    Fl_Box *sky_placeholder, *sky_detached_detail=nullptr;
    Fl_Button *sky_detach, *sky_fullscreen=nullptr;
    std::vector<int> sky_rows;
    Sight sky_at{};
    Fl_Input *book_year;
    Fl_Check_Button *book_sections[8];
    Fl_Check_Button *book_shade_rows;
    std::thread book_worker;
    std::atomic<bool> book_cancel{false}, book_done{false};
    std::atomic<int> book_percent{0};
    bool book_running = false;
    std::string book_error, book_path;
    std::unique_ptr<Fl_Double_Window> book_window;
    Fl_Progress *book_progress = nullptr;
    std::shared_ptr<LandMap> land_map;
    std::thread map_worker;
    std::atomic<bool> map_cancel{false}, map_done{false};
    std::atomic<uint64_t> map_bytes{0};
    bool map_downloading = false;
    std::string map_download_error;
    std::shared_ptr<LandMap> downloaded_map;
    Fl_Text_Buffer al_buffer, report_buffer, fix_buffer, chart_buffer;
    Fl_Text_Buffer index_buffer;
    SightClusterPlot *cluster_plot;
    Fl_Check_Button *cluster_reject;
    Fl_Box *cluster_summary;
    Fl_Button *cluster_save_button, *cluster_replace_button, *cluster_preview_button;
    std::vector<Fl_Widget *> chart_controls, cluster_controls;
    std::vector<int> cluster_ids;
    NavSightCluster cluster{};
    bool showing_cluster=false;
    std::unique_ptr<Fl_Double_Window> index_window;
    Fl_Text_Display *al_display, *report_display, *fix_display, *chart_display;
    SightStore store{};
    int edit_id = 0;
    bool testing = false;
    std::vector<Sight> fix_sights;
    std::vector<Reduction> fix_red;
    FixResult fix_result{};
    std::vector<std::unique_ptr<std::function<void()>>> callbacks;
    Fl_Button *reduce_button, *save_button, *update_button, *fix_button, *index_button;
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
        auto *name = new Fl_Box(26, 12, 500, 29, "NavAlm  /  Navigation Workbench");
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
        button(36, 270, 160, "Use saved DR", [this] { load_saved_position(al_lat, al_lat_hemi, al_lon, al_lon_hemi); });
        button(210, 270, 180, "Use current sight DR", [this] { copy_sight_position(al_lat, al_lat_hemi, al_lon, al_lon_hemi); });
        button(405, 270, 225, "Use object for a sight", [this] { almanac_to_sight(); });
        al_display = display(36, 338, 1105, 437, al_buffer);
        auto *al_angle_hint = new Fl_Box(36, 306, 1105, 24,
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
        horizon = choice(36, 416, 150, "Horizon", "Sea horizon|Artificial horizon|Short of horizon");
        short_distance = input(196,416,110,"Waterline · NM");
        short_distance->tooltip("Distance in nautical miles to the nearer waterline used as a reference.\nOnly for Short of horizon; use height of eye as well.");
        limb = choice(326, 416, 150, "Limb", "Lower limb|Center|Upper limb");
        ie = input(36, 478, 100, "Index error · min");
        arc = choice(146, 478, 100, "Index error arc", "Off arc|On arc");
        height = input(266, 478, 110, "Height of eye");
        units = choice(386, 478, 90, "Units", "Feet|Metres");
        temp = input(36, 540, 210, "Temperature · Celsius");
        press = input(266, 540, 210, "Pressure · millibars");
        at_dr = new Fl_Check_Button(36, 574, 430, 22, "Reduce at DR (no whole-degree LHA)");
        at_dr->labelsize(13);
        motion = new Fl_Check_Button(36, 601, 430, 22,
                                     "Advance this LOP using vessel motion and current");
        motion->labelsize(13);
        course = input(36, 650, 100, "Course · true");
        speed = input(149, 650, 100, "Speed · kn");
        set = input(262, 650, 100, "Set · true");
        drift = input(375, 650, 101, "Drift · kn");
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
        report_display = display(505, 188, 632, 360, report_buffer);
        sight_watch_date=input(505,588,230,"Watch date · UTC scale");
        sight_watch_date->tooltip("Watch date as YYYY-MM-DD, on the UTC scale.");
        sight_watch_time=input(750,588,387,"Watch time · HH:MM:SS.sss");
        sight_watch_time->tooltip("Enter the original watch reading here. Check Use derived correction to calculate the UTC fields from this reading.");
        sight_clock=choice(505,654,230,"Chrono clock · watch set to UTC","");
        sight_clock_apply=new Fl_Check_Button(750,654,387,32,"Use derived correction from Chrono");
        sight_clock_apply->labelsize(13);
        sight_clock_status=new Fl_Box(505,696,632,34,"Chrono is off; the entered time is UTC.");
        sight_clock_status->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);sight_clock_status->labelsize(12);
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
                                "Fix: choose sights on different bearings (3 preferred).\nCluster: choose a series of the same body.");
        hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        hint->labelsize(13);
        saved = new Fl_Multi_Browser(36, 190, 430, 215);
        saved->textsize(13);
        saved->textfont(FL_COURIER);
        saved->format_char('\0');
        saved->tooltip("Use the platform's multi-select key or Shift to select several rows.");
        button(36, 420, 94, "Edit sight", [this] { edit_sight(); });
        button(142, 420, 94, "Delete…", [this] { delete_sights(); });
        button(248, 420, 100, "Select all", [this] {
            for (int i = 1; i <= saved->size(); ++i)
                saved->select(i);
        });
        button(360, 420, 106, "Reload log", [this] { load_store(); });
        cluster_preview_button=button(36,461,208,"Plot altitude cluster", [this]{ preview_cluster(); });
        index_button=button(256,461,210,"Estimate index error…", [this]{ estimate_index(); });
        index_button->tooltip("Compare at least three raw sights from one sextant at known positions.");
        button(36,503,208,"Back up sight log…", [this]{ backup_sights(); });
        button(256,503,210,"Restore sight log…", [this]{ restore_sights(); });
        fix_at = new Fl_Check_Button(36, 544, 430, 27,
                                     "Advance to explicit UTC fix time (otherwise latest sight)");
        fix_at->labelsize(12);
        fix_date = input(36, 598, 210, "Fix date · YYYY-MM-DD");
        fix_time = input(266, 598, 200, "Fix time · HH:MM:SS");
        fix_button =
            button(36, 639, 208, "Plot selected fix", [this] { calculate_fix(); }, true);
        button(256, 639, 210, "Export fix report…", [this] { export_text(fix_buffer, "fix.txt"); });
        fix_display = display(36, 683, 430, 92, fix_buffer);
        fix_display->textsize(12);
        const int chart_begin=fix_tab->children();
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
        const int chart_end=fix_tab->children();
        for(int i=chart_begin;i<chart_end;i++){
            auto *w=fix_tab->child(i);
            if(w!=plot && w!=detached_hint)chart_controls.push_back(w);
        }
        cluster_plot=new SightClusterPlot(493,183,645,512);
        cluster_plot->tooltip("Corrected observed altitude (Ho) versus UTC. The line follows the ephemeris plus the fitted intercept.");
        button(493,141,150,"Show fix chart",[this]{ show_cluster(false); });
        cluster_reject=new Fl_Check_Button(661,144,460,28,"Exclude obvious outliers (5 or more sights)");
        cluster_reject->value(1);
        bind(cluster_reject,[this]{ preview_cluster(); });
        cluster_summary=new Fl_Box(493,698,645,42);
        cluster_summary->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);
        cluster_summary->labelsize(12);
        cluster_save_button=button(493,749,257,"Save representative · keep originals",[this]{ save_cluster(false); },true);
        cluster_replace_button=button(769,749,247,"Replace selected observations…",[this]{ save_cluster(true); });
        for(int i=chart_end;i<fix_tab->children();i++){
            auto *w=fix_tab->child(i); cluster_controls.push_back(w); w->hide();
        }
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
        sky_tab = new Fl_Group(16,130,1148,672,"Planetarium");
        sky_date=input(36,166,175,"UTC date · YYYY-MM-DD");
        sky_time=input(225,166,140,"UTC time · HH:MM:SS");
        sky_lat=input(380,166,140,"DR latitude · DD MM.m");
        sky_lat_hemi=choice(528,166,50,"","N|S");
        sky_lon=input(593,166,150,"DR longitude · DDD MM.m");
        sky_lon_hemi=choice(751,166,50,"","E|W");
        button(817,166,100,"Now UTC",[this]{now(sky_date,sky_time);update_sky();});
        button(931,166,210,"Use current sight DR",[this]{
            copy_sight_position(sky_lat,sky_lat_hemi,sky_lon,sky_lon_hemi);
        });
        sky_min=input(36,232,100,"Min altitude deg");sky_min->value("15");
        sky_max=input(150,232,100,"Max altitude deg");sky_max->value("75");
        sky_mag=input(264,232,100,"Faintest magnitude");sky_mag->value("3.0");
        sky_count=choice(378,232,100,"Suggested set","3 stars|4 stars");
        button(496,232,130,"Update sky",[this]{update_sky();},true);
        button(640,232,240,"Next morning sights",[this]{update_sky(1);});
        button(894,232,247,"Next evening sights",[this]{update_sky(0);});
        sky_status=new Fl_Box(36,274,1105,35,"Enter a DR and UTC time. All 58 catalogue stars include Polaris.");
        sky_status->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);sky_status->labelsize(13);
        sky_names=new Fl_Check_Button(36,308,375,25,"Star names (bodies always labelled)");sky_names->value(1);
        bind(sky_names,[this]{update_sky_layers();});
        sky_detach=button(425,306,176,"Detach sky…",[this]{
            if(sky_window && sky_window->shown())reattach_sky();else detach_sky();
        });
        const char *layer_labels[]={"Constellations","Constellation names","Supporting stars"};
        for(int i=0;i<3;i++){
            sky_layers[i]=new Fl_Check_Button(36+i*190,340,190,25,layer_labels[i]);
            sky_layers[i]->value(i!=1);bind(sky_layers[i],[this]{update_sky_layers();});
        }
        button(615,340,175,"Use saved DR",[this]{load_saved_position(sky_lat,sky_lat_hemi,sky_lon,sky_lon_hemi);});
        sky_placeholder=new Fl_Box(36,406,565,348,"Sky is open in its own window.\nUse Reattach sky to return it here.");
        sky_placeholder->hide();
        sky_canvas=new SkyCanvas(36,406,565,348);
        sky_canvas->view_changed=[this]{update_sky_zoom();};
        sky_zoom_controls(36,372,false);
        sky_canvas->choose=[this](int index){select_sky_star(index);};
        sky_below=new Fl_Check_Button(625,308,320,25,"Include below-horizon objects in list");
        bind(sky_below,[this]{render_sky_list();});
        auto *sky_columns=new Fl_Box(625,376,515,23,"   Object               Hc        Zn T     Mag");
        sky_columns->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);sky_columns->labelfont(FL_COURIER);
        sky_columns->labelsize(12);
        sky_list=new Fl_Hold_Browser(625,402,515,232);
        sky_list->textfont(FL_COURIER);sky_list->textsize(12);sky_list->format_char(0);
        bind(sky_list,[this]{int row=sky_list->value();if(row>0 && row<=int(sky_rows.size()))select_sky_star(sky_rows[row-1]);});
        sky_summary=new Fl_Box(625,642,515,56,"");
        sky_summary->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);sky_summary->labelsize(12);
        sky_detail=new Fl_Box(625,700,515,40,"Select an object on the chart or in the list.");
        sky_detail->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);sky_detail->labelsize(13);
        sky_sight=button(625,748,515,"Use selected object for a sight",[this]{sky_to_sight();});
        sky_sight->deactivate();
        auto *sky_hint=new Fl_Box(36,758,565,38,"Wheel / + / −: zoom · drag: pan · 0: all sky (zenith at centre).\nN up / E left. Geocentric Hc; no refraction/parallax. Symbolic discs.");
        sky_hint->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);sky_hint->labelsize(11);
        sky_tab->end();

        defaults_tab=new Fl_Group(16,130,1148,672,"Defaults");
        auto *def_title=new Fl_Box(36,146,1080,32,"Starting settings for new sights and saved DR for the planning pages");
        def_title->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);def_title->labelfont(FL_HELVETICA_BOLD);def_title->labelsize(17);
        def_lat=input(36,210,180,"DR latitude · DD MM.m");def_lat_hemi=choice(226,210,60,"","N|S");
        def_lon=input(310,210,180,"DR longitude · DDD MM.m");def_lon_hemi=choice(500,210,60,"","E|W");
        def_horizon=choice(36,280,250,"Horizon","Sea horizon|Artificial horizon (halve Hs)|Short of horizon (waterline)");
        def_short=input(310,280,250,"Distance to waterline · NM");
        def_height=input(36,350,180,"Eye height for dip");def_units=choice(226,350,60,"","ft|m");
        def_limb=choice(310,350,250,"Preferred limb","Lower limb|Center|Upper limb");
        def_ie=input(36,420,180,"Index error · arcminutes");def_arc=choice(226,420,150,"","Off arc (+IC)|On arc (−IC)");
        def_temp=input(36,490,250,"Temperature · Celsius");def_press=input(310,490,250,"Pressure · millibars");
        def_at_dr=new Fl_Check_Button(36,546,520,26,"Reduce at DR (no whole-degree LHA)");
        def_motion=new Fl_Check_Button(36,590,530,26,"Use vessel motion and current for running fixes");
        def_course=input(36,658,120,"Course · true");def_speed=input(170,658,120,"Speed · kn");
        def_set=input(304,658,120,"Set · true");def_drift=input(438,658,120,"Drift · kn");
        button(36,732,160,"Save defaults",[this]{save_setup_defaults();},true);
        button(210,732,160,"Reload saved",[this]{fill_defaults();});
        button(384,732,176,"Use sight settings",[this]{fill_defaults_from_sight(read_sight(false),true);});
        auto *def_help=new Fl_Box(610,205,515,320,
            "Saved DR loads into Sight reduction, Almanac and Planetarium at startup.\n\n"
            "The planning pages can reload saved DR or copy the current sight DR. Changing one page does not overwrite another page.\n\n"
            "Eye height gives dip; for a nearer waterline choose Short of horizon and enter its distance in nautical miles. Artificial horizon halves Hs and applies no dip.\n\n"
            "On-arc index error subtracts; off-arc adds. Set is the direction the current flows toward; drift is its speed.\n\n"
            "UTC starts at the current time. Hs is entered for each observation. AP is calculated for each sight; direct reduction uses DR as AP.");
        def_help->align(FL_ALIGN_TOP|FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);def_help->labelsize(14);
        button(610,560,370,"Load saved DR into all three pages",[this]{
            load_saved_position(lat,lat_hemi,lon,lon_hemi);
            load_saved_position(al_lat,al_lat_hemi,al_lon,al_lon_hemi);
            load_saved_position(sky_lat,sky_lat_hemi,sky_lon,sky_lon_hemi);
        });
        for(auto *widget:{static_cast<Fl_Widget*>(def_horizon),static_cast<Fl_Widget*>(def_motion)})
            bind(widget,[this]{defaults_state();});
        defaults_tab->end();

        lunar=new LunarPanel;
        lunar->notify=[this](const std::string &s,bool error){message(s,error);};
        lunar->current_position=[this]{Sight s{};coordinates(lat,lat_hemi,lon,lon_hemi,s);s.has_motion=motion->value();
            if(s.has_motion){s.course=number(course->value(),"Course",0,360);s.speed=number(speed->value(),"Speed",0,100);
                s.set=number(set->value(),"Set",0,360);s.drift=number(drift->value(),"Drift",0,100);}return s;};
        lunar->chrono_estimate=[this](double watch){chrono_guard();return clock_log.model(clock_log.selected).utc_from_watch(watch)-watch;};
        lunar->chrono_transfer=[this](const NavLunarRequest &q,const NavLunarSolution &s){
            chrono_guard();clock_log.clock(clock_log.selected);chrono_cancel_edit(true);
            double watch=chrono_nav::epoch(q.distance);auto w=chrono_nav::stamp(watch),u=chrono_nav::stamp(watch+s.correction_sec);
            chrono_watch_date->value(w.substr(0,10).c_str());chrono_watch_time->value(w.substr(11).c_str());
            chrono_ref_date->value(u.substr(0,10).c_str());chrono_ref_time->value(u.substr(11).c_str());chrono_source->value(1);
            std::string note=std::string("Lunar / ")+q.distance.body+"; formal sigma "+fmt(s.sigma_time_sec,1)+" s; "+(q.refine_position?"real-altitude fix":"fixed AP")+"; verify systematics";
            chrono_note->value(note.c_str());tabs->value(chrono_tab);
            message("Lunar comparison prepared. Review its uncertainty and clock, then Add comparison to log it.");
        };

        chrono_tab=new Fl_Group(16,130,1148,672,"Chrono");
        chrono_clock=choice(36,186,240,"Watch / clock","");
        chrono_name=input(294,186,240,"New clock name");chrono_name->maximum_size(31);
        button(550,186,130,"Add clock",[this]{
            chrono_guard();auto next=clock_log;next.add_clock(chrono_name->value());chrono_commit(next);chrono_name->value("");
        });
        button(696,186,190,"Clock reset / new series",[this]{
            chrono_guard();auto next=clock_log;auto &c=next.clock(next.selected);
            if(!testing && fl_choice("Start a new drift series? Earlier comparisons stay in the log.","Cancel","New series",nullptr)!=1)return;
            ++c.series;chrono_commit(next);
        });
        button(902,186,230,"Reload clock log",[this]{chrono_load();});
        chrono_ref_date=input(36,260,190,"Reference date · UTC");chrono_ref_time=input(242,260,190,"Reference time · HH:MM:SS.sss");
        chrono_watch_date=input(448,260,190,"Watch date · UTC");chrono_watch_time=input(654,260,190,"Watch reading · HH:MM:SS.sss");
        chrono_source=choice(860,260,272,"Reference source","Manual WWV|Other UTC reference|Computer clock");
        chrono_note=input(36,326,460,"Note (optional)");chrono_note->maximum_size(160);
        button(512,326,200,"Computer UTC now",[this]{chrono_computer_now();});
        chrono_cancel_button=button(728,326,150,"Cancel edit",[this]{chrono_cancel_edit(true);});chrono_cancel_button->deactivate();
        chrono_add_button=button(894,326,238,"Add comparison",[this]{chrono_add();},true);
        chrono_summary=new Fl_Box(36,378,1096,64,"");
        chrono_summary->align(FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);chrono_summary->labelsize(14);
        chrono_plot=new ChronoPlot(36,452,540,242);
        chrono_plot->select=[this](size_t index){chrono_select(index);};
        auto *columns=new Fl_Box(596,452,536,22,"   Series  Reference UTC             Error (s)   Source / note");
        columns->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);columns->labelfont(FL_COURIER);columns->labelsize(12);
        chrono_rows=new Fl_Hold_Browser(596,476,536,170);chrono_rows->textfont(FL_COURIER);chrono_rows->textsize(12);chrono_rows->format_char(0);
        bind(chrono_rows,[this]{chrono_select(chrono_selected_index());});
        chrono_edit_button=button(596,660,100,"Edit selected",[this]{chrono_begin_edit();});
        chrono_delete_button=button(708,660,100,"Delete selected",[this]{chrono_delete();});
        chrono_edit_button->labelsize(12);chrono_delete_button->labelsize(12);
        chrono_exclude_button=button(820,660,170,"Exclude / restore",[this]{
            chrono_guard();auto index=chrono_selected_index();
            auto next=clock_log;next.set_enabled(index,!next.readings[index].enabled);chrono_commit(next);chrono_select(index);
        });
        button(1002,660,130,"Export log…",[this]{
            chrono_guard();auto path=choose_file("Export clock comparisons","CSV\t*.csv",true,"NavAlm-clock-log.csv");
            if(path.empty())return;
            auto csv=[](const std::string &s){std::string out="\"";for(char c:s){out+=c;if(c=='\"')out+='\"';}return out+'\"';};
            std::ostringstream out;out<<"clock,series,reference_utc,watch_utc,error_seconds,source,note,included\n"<<std::setprecision(12);
            for(const auto &r:clock_log.readings)out<<csv(clock_log.clock(r.clock).name)<<','<<r.series<<','<<chrono_nav::stamp(r.utc)<<','<<chrono_nav::stamp(r.watch)<<','<<(r.watch-r.utc)<<','<<csv(r.source)<<','<<csv(r.note)<<','<<r.enabled<<'\n';
            write_file(path,out.str());message("Clock comparison CSV saved: "+path);
        });
        chrono_apply_new=new Fl_Check_Button(36,704,1096,30,"Use selected clock for new manual sights (time starts blank)");chrono_apply_new->labelsize(13);
        bind(chrono_apply_new,[this]{chrono_guard();auto next=clock_log;next.apply_new=chrono_apply_new->value();if(next.apply_new)next.model(next.selected).utc_from_watch(std::time(nullptr));chrono_commit(next);});
        auto *help=new Fl_Box(36,744,1096,44,
            "Log the watch reading and reference UTC at the same instant. Positive error = fast; positive drift = gaining seconds/day.\n"
            "Select a point or row, then Edit, Delete or Exclude. Exclusion keeps its history; deleting removes it after confirmation.\n"
            "The plot follows the selected row's series. Only the current series supplies sight corrections. See CHRONO.md for WWV and computer UTC notes.");
        help->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);help->labelsize(12);
        bind(chrono_clock,[this]{chrono_guard();auto next=clock_log;int i=chrono_clock->value();if(i<0||i>=int(next.clocks.size()))return;next.selected=next.clocks[i].id;chrono_commit(next);});
        bind(sight_clock_apply,[this]{sight_clock_status->copy_label(sight_clock_apply->value()?"Watch date/time will determine UTC using the selected Chrono clock.":"Chrono is off; the UTC fields are used directly.");});
        chrono_tab->end();

        book_tab = new Fl_Group(16, 130, 1148, 672, "Annual book");
        auto *book_title = new Fl_Box(36, 148, 1080, 35, "Build a custom annual practice almanac");
        book_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        book_title->labelfont(FL_HELVETICA_BOLD);
        book_title->labelsize(20);
        book_year = input(36, 219, 195, "Calendar year · YYYY");
        book_year->value("2027");
        auto *book_help = new Fl_Box(263, 196, 860, 67,
            "Choose sections independently. Export directly to a printable PDF.\n"
            "Direct PDF needs no TeX. Optional LaTeX source is compiled separately.");
        book_help->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        book_help->labelsize(14);
        const char *section_labels[] = {
            "Sun and Moon hourly pages", "Aries and navigational planets", "Navigational star positions",
            "Greenwich rise, set and twilight", "Increments and hourly-rate corrections",
            "Dip, refraction and parallax components", "Concise calculator guide (not NAO tables)",
            "Compact Ageton tables + worksheet (26 pages)"};
        for(int i=0;i<8;++i) {
            book_sections[i] = new Fl_Check_Button(36+(i/4)*545, 290+(i%4)*52, 535, 34, section_labels[i]);
            book_sections[i]->labelsize(15);
            book_sections[i]->value(1);
        }
        book_shade_rows = new Fl_Check_Button(36, 485, 1090, 30,
            "Alternate row shading (hourly + reference tables)");
        book_shade_rows->labelsize(15);
        book_shade_rows->value(0);
        auto *book_note = new Fl_Box(36, 521, 1090, 116,
            "5.4 PRACTICE BOOK\n"
            "Ageton can be added, substituted for the calculator guide, or omitted.\n"
            "NAO Concise lookup tables and several official-book sections are still to come.\n"
            "For learning and recreation. For navigation, buy and use the official Nautical Almanac.");
        book_note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        book_note->labelsize(14);
        button(36, 668, 230, "Export annual PDF…", [this] { export_book(); }, true);
        button(282, 668, 230, "Export LaTeX source…", [this] { export_book(navbook::Format::Latex); });
        auto *book_hint = new Fl_Box(535, 659, 585, 58,
            "PDF: ready to print. LaTeX: editable source; compile with TeX.\n"
            "Generation shows progress and can be cancelled.");
        book_hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        book_hint->labelsize(13);
        book_tab->end();
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
                if(app->sky_window)app->sky_window->hide();
                w->hide();
            },
            this);
        chrono_load();
        now(chrono_ref_date,chrono_ref_time);now(chrono_watch_date,chrono_watch_time);
        chrono_ref_time->value("");chrono_watch_time->value("");
        new_sight();
        fill_defaults();
        now(al_date, al_time);
        now(sky_date,sky_time);
        load_saved_position(al_lat,al_lat_hemi,al_lon,al_lon_hemi,true);
        load_saved_position(sky_lat,sky_lat_hemi,sky_lon,sky_lon_hemi,true);
        for (auto *field : {lat, al_lat, sky_lat})
            field->tooltip("Degrees SPACE minutes: 34 02.5 = 34 degrees 2.5 minutes.\nDecimal "
                           "degrees are also accepted. Select N or S separately.");
        for (auto *field : {lon, al_lon, sky_lon})
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
        Fl::remove_timeout(book_tick, this);
        book_cancel = true;
        if (book_worker.joinable()) book_worker.join();
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
    static std::string sky_angle(double value) {
        long tenth=std::lround(std::abs(value)*600);
        char text[32];
        std::snprintf(text,sizeof text,"%s%02ld %04.1f",value<0?"-":"",tenth/600,(tenth%600)/10.);
        return text;
    }
    void update_sky_layers() {
        sky_canvas->names=sky_names->value();sky_canvas->lines=sky_layers[0]->value();
        sky_canvas->constellation_names=sky_layers[1]->value();sky_canvas->supporting=sky_layers[2]->value();
        if(sky_detached_names)sky_detached_names->value(sky_names->value());
        for(int i=0;i<3;i++)if(sky_detached_layers[i])sky_detached_layers[i]->value(sky_layers[i]->value());
        sky_canvas->redraw();
    }
    void update_sky_zoom() {
        std::string text=fmt(sky_canvas->zoom(),2)+"×";
        if(sky_zoom_readout)sky_zoom_readout->copy_label(text.c_str());
        if(sky_detached_zoom_readout)sky_detached_zoom_readout->copy_label(text.c_str());
    }
    void sky_zoom_controls(int x,int y,bool detached) {
        button(x,y,86,"Zoom +",[this]{sky_canvas->zoom_by(1.3);});
        button(x+94,y,86,"Zoom −",[this]{sky_canvas->zoom_by(1/1.3);});
        button(x+188,y,90,"All sky",[this]{sky_canvas->reset_view();});
        button(x+286,y,160,"Centre selected",[this]{sky_canvas->centre_selected();});
        auto *readout=new Fl_Box(x+454,y,100,32,"");readout->labelsize(12);
        if(detached)sky_detached_zoom_readout=readout;else sky_zoom_readout=readout;
        update_sky_zoom();
    }
    void toggle_sky_fullscreen() {
        if(sky_window->fullscreen_active())sky_window->fullscreen_off();else sky_window->fullscreen();
        sky_fullscreen->copy_label(sky_window->fullscreen_active()?"Windowed (F11)":"Full screen (F11)");
    }
    void reattach_sky() {
        if(!sky_window || sky_canvas->parent()!=sky_window.get())return;
        if(sky_window->fullscreen_active())toggle_sky_fullscreen();
        sky_window->resizable(nullptr);sky_window->remove(sky_canvas);sky_tab->add(sky_canvas);
        sky_canvas->resize(sky_placeholder->x(),sky_placeholder->y(),sky_placeholder->w(),sky_placeholder->h());
        sky_window->hide();sky_placeholder->hide();sky_canvas->show();
        sky_detach->copy_label("Detach sky…");sky_tab->redraw();
    }
    void detach_sky() {
        if(!sky_window){
            sky_window=std::make_unique<SkyWindow>(1040,850,"NavAlm — Planetarium");
            sky_window->begin();
            sky_detached_names=new Fl_Check_Button(12,10,120,26,"Star names");
            bind(sky_detached_names,[this]{sky_names->value(sky_detached_names->value());update_sky_layers();});
            const char *labels[]={"Constellations","Constellation names","Supporting stars"};
            for(int i=0;i<3;i++){
                sky_detached_layers[i]=new Fl_Check_Button(140+i*215,10,215,26,labels[i]);
                bind(sky_detached_layers[i],[this,i]{sky_layers[i]->value(sky_detached_layers[i]->value());update_sky_layers();});
            }
            sky_fullscreen=button(12,42,190,"Full screen (F11)",[this]{toggle_sky_fullscreen();});
            button(214,42,150,"Reattach sky",[this]{reattach_sky();});
            button(376,42,220,"Use selected for a sight",[this]{sky_to_sight();if(tabs->value()==sight_tab)reattach_sky();});
            sky_detached_detail=new Fl_Box(608,42,420,34,"Click a navigation star or body for details.");
            sky_detached_detail->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP);sky_detached_detail->labelsize(12);
            sky_zoom_controls(12,76,true);
            sky_window->end();sky_window->size_range(1040,600);
            sky_window->toggle_fullscreen=[this]{toggle_sky_fullscreen();};
            sky_window->callback([](Fl_Widget*,void *p){static_cast<Workbench*>(p)->reattach_sky();},this);
        }
        sky_tab->remove(sky_canvas);sky_window->add(sky_canvas);
        sky_canvas->resize(0,112,sky_window->w(),sky_window->h()-112);
        sky_window->resizable(sky_canvas);sky_placeholder->show();
        sky_detach->copy_label("Reattach sky");update_sky_layers();
        sky_detached_detail->copy_label(sky_detail->label());
        sky_window->show();sky_canvas->show();sky_canvas->take_focus();
    }
    void invalidate_sky() {
        sky_canvas->ready=false;sky_canvas->redraw();sky_list->clear();sky_rows.clear();
        sky_sight->deactivate();sky_summary->copy_label("");sky_detail->copy_label("");
        if(sky_detached_detail)sky_detached_detail->copy_label("Update sky to recalculate.");
        sky_status->copy_label("Engine changed. Update sky to recalculate the planner.");
    }
    void render_sky_list() {
        sky_list->clear(); sky_rows.clear();
        if(!sky_canvas->ready) return;
        for(size_t i=0;i<sky_canvas->object_count();i++)
            if(sky_below->value() || sky_canvas->object(i).altitude>0) sky_rows.push_back(int(i));
        std::stable_sort(sky_rows.begin(),sky_rows.end(),[this](int a,int b){
            const auto &x=sky_canvas->object(a), &y=sky_canvas->object(b);
            if(x.is_body!=y.is_body) return x.is_body>y.is_body;
            if(x.selected!=y.selected) return x.selected>y.selected;
            if(x.eligible!=y.eligible) return x.eligible>y.eligible;
            return x.azimuth<y.azimuth;
        });
        for(size_t row=0;row<sky_rows.size();row++){
            int i=sky_rows[row]; const auto &s=sky_canvas->object(i);
            char text[200];
            std::snprintf(text,sizeof text,"%c %-18s %8s  %6.1f  %4.1f",
                s.selected?'*':' ',s.name,sky_angle(s.altitude).c_str(),s.azimuth,s.magnitude);
            sky_list->add(text);
            if(i==sky_canvas->selected) sky_list->value(int(row)+1);
        }
    }
    void select_sky_star(int index) {
        if(!sky_canvas->ready || index<0 || index>=int(sky_canvas->object_count())) return;
        sky_canvas->selected=index;sky_canvas->redraw();
        const auto &s=sky_canvas->object(index);
        std::string detail=std::string(s.name)+" · Hc "+sky_angle(s.altitude)+" · Zn "+
            (s.azimuth_defined?fmt(s.azimuth,1)+" T":"undefined at zenith")+" · mag "+fmt(s.magnitude,1);
        if(!s.is_body && s.constellation && *s.constellation)detail+=" · "+std::string(s.constellation);
        sky_detail->copy_label(detail.c_str());
        if(sky_detached_detail)sky_detached_detail->copy_label(detail.c_str());
        if(s.altitude>0) sky_sight->activate(); else sky_sight->deactivate();
        for(size_t row=0;row<sky_rows.size();row++) if(sky_rows[row]==index) sky_list->value(int(row)+1);
    }
    void update_sky(int morning=-1) {
        try {
            if(book_running) throw std::runtime_error("Wait for the annual book to finish.");
            Sight at{};date_time(sky_date->value(),sky_time->value(),at);
            if(!sky_lat->value()[0] || !sky_lon->value()[0])
                throw std::runtime_error("Enter DR latitude and longitude, or copy the current sight DR.");
            coordinates(sky_lat,sky_lat_hemi,sky_lon,sky_lon_hemi,at);
            NavSkyOptions options{};
            options.jd=navsight_jd(&at);options.latitude=at.dr_lat;options.longitude=at.dr_lon;
            options.min_altitude=number(sky_min->value(),"Minimum altitude",0,90);
            options.max_altitude=number(sky_max->value(),"Maximum altitude",0,90);
            options.max_magnitude=number(sky_mag->value(),"Faintest magnitude",-2,6);
            options.requested=sky_count->value()?4:3;
            char error[512]{};
            if(morning>=0){
                if(navsky_twilight(options.jd,at.dr_lat,at.dr_lon,morning,&options.jd,error,sizeof error))
                    throw std::runtime_error(error);
                char d[32],t[32];navsky_utc(options.jd,d,sizeof d,t,sizeof t);
                sky_date->value(d);sky_time->value(t);date_time(d,t,at);
            }
            if(navsky_compute(&options,&sky_canvas->sky,error,sizeof error)) throw std::runtime_error(error);
            sky_at=at;sky_canvas->ready=true;
            auto &sky=sky_canvas->sky;
            std::string summary=std::to_string(sky.count)+" stars + Sun, Moon, 5 planets · "+sky.light+
                " · Sun "+fmt(sky.sun_altitude,1)+" deg · "+nav_engine_name();
            if(sky.sun_altitude> -6) summary+=" · Bright sky: stars may be hard to see.";
            else if(sky.sun_altitude< -12) summary+=" · Sea horizon may be hard to see.";
            sky_status->copy_label(summary.c_str());
            std::string plan=std::to_string(sky.selected)+" suggested / "+std::to_string(sky.eligible)+
                " eligible. Bearing geometry "+fmt(sky.geometry*100,0)+" / 100.\n"+
                (sky.selected<options.requested?"Too few eligible stars. ":"")+std::string("* = suggested. Spread, brightness and moderate Hc guide selection.");
            sky_summary->copy_label(plan.c_str());
            render_sky_list();
            if(sky_canvas->selected>=0) select_sky_star(sky_canvas->selected);
            sky_canvas->redraw();
            message("Sky updated · next sight-window buttons seek Sun -8 deg within 48 hours.");
        } catch(...) {
            sky_canvas->ready=false;sky_canvas->redraw();sky_list->clear();sky_rows.clear();
            if(sky_detached_detail)sky_detached_detail->copy_label("Sky unavailable. Correct entries and update.");
            sky_status->copy_label("Sky unavailable. Correct the entries and update again.");
            sky_summary->copy_label("");sky_detail->copy_label("");sky_sight->deactivate();throw;
        }
    }
    void sky_to_sight() {
        int index=sky_canvas->selected;
        if(!sky_canvas->ready || index<0 || index>=int(sky_canvas->object_count()) ||
           sky_canvas->object(index).altitude<=0) return;
        const auto object=sky_canvas->object(index);
        Sight at=sky_at;
        bool direct=at_dr->value();
        new_sight();sight_clock_apply->value(0);at_dr->value(direct);body->value(object.is_body?int(object.body):7);
        sight_clock_status->copy_label("Planetarium time is already UTC; Chrono is off.");
        if(!object.is_body) star->value(index);
        else if(object.body!=NAV_SUN && object.body!=NAV_MOON)limb->value(1);
        char planned_date[32],planned_time[32];
        navsky_utc(navsight_jd(&at),planned_date,sizeof planned_date,planned_time,sizeof planned_time);
        date->value(planned_date);time->value(planned_time);
        lat->value(angle_entry(at.dr_lat,2).c_str());lat_hemi->value(at.dr_lat<0?1:0);
        lon->value(angle_entry(at.dr_lon,3).c_str());lon_hemi->value(at.dr_lon<0?1:0);
        hs->value("");body_state();tabs->value(sight_tab);
        message("Planning "+std::string(object.name)+" · enter your measured Hs before reducing.");
    }
    static void book_tick(void *p) { static_cast<Workbench *>(p)->poll_book(); }
    void begin_book(const navbook::Options &options, const std::string &path) {
        if(book_running) throw std::runtime_error("An annual book is already being generated.");
        book_path=path; book_error.clear(); book_cancel=false; book_done=false; book_percent=0;
        book_window=std::make_unique<Fl_Double_Window>(540, 185, "NavAlm annual book");
        book_window->begin();
        book_progress=new Fl_Progress(25,30,490,35,"Preparing annual pages…");
        book_progress->minimum(0); book_progress->maximum(100); book_progress->selection_color(teal);
        auto *hint=new Fl_Box(25,76,490,28,"The astronomy engine is reserved until generation completes.");
        hint->labelsize(12);
        button(190,120,160,"Cancel generation",[this] { book_cancel=true; });
        book_window->end();
        book_window->callback([](Fl_Widget *,void *p) {static_cast<Workbench *>(p)->book_cancel=true;},this);
        tabs->deactivate(); engine->deactivate(); book_running=true;
        try {
            book_worker=std::thread([this,options,path] {
                try {
                    auto pdf=navbook::generate(options,[this](int percent,const std::string &) {
                        book_percent=percent; return !book_cancel.load();
                    });
                    if(book_cancel) throw std::runtime_error("Book generation cancelled.");
                    navbook::save(path,pdf);
                } catch(const std::exception &e) {book_error=e.what();}
                book_done.store(true,std::memory_order_release);
            });
        } catch(...) {
            book_running=false; tabs->activate(); engine->activate(); book_window.reset(); throw;
        }
        book_window->show(); Fl::add_timeout(.1,book_tick,this);
    }
    void poll_book() {
        if(!book_running) return;
        book_progress->value(book_percent.load());
        book_progress->copy_label(("Generating book: "+std::to_string(book_percent.load())+"%").c_str());
        if(!book_done.load(std::memory_order_acquire)) {Fl::repeat_timeout(.1,book_tick,this);return;}
        if(book_worker.joinable()) book_worker.join();
        book_running=false; book_window->hide(); book_window.reset(); book_progress=nullptr;
        tabs->activate(); engine->activate();
        message(book_error.empty()?"Annual practice book saved: "+book_path:book_error,
                !book_error.empty() && book_error!="Book generation cancelled.");
    }
    void export_book(navbook::Format format=navbook::Format::Pdf) {
        navbook::Options o; o.format=format;
        o.shade_rows=book_shade_rows->value();
        double year=number(book_year->value(),"Almanac year",1600,2200);
        if(std::floor(year)!=year) throw std::runtime_error("Almanac year must be a whole number.");
        o.year=int(year);
        o.sun_moon=book_sections[0]->value(); o.planets=book_sections[1]->value();
        o.stars=book_sections[2]->value(); o.events=book_sections[3]->value();
        o.increments=book_sections[4]->value(); o.corrections=book_sections[5]->value();
        bool formulas=book_sections[6]->value(), ageton=book_sections[7]->value();
        o.reduction=formulas?(ageton?navbook::Reduction::Both:navbook::Reduction::Formulas):
                              (ageton?navbook::Reduction::Ageton:navbook::Reduction::None);
        if(!(o.sun_moon||o.planets||o.stars||o.events||o.increments||o.corrections||formulas||ageton))
            throw std::runtime_error("Select at least one book section.");
        bool tex=format==navbook::Format::Latex;
        auto path=choose_file(tex?"Export LaTeX source":"Export annual practice almanac",
            tex?"LaTeX\t*.tex":"PDF\t*.pdf",true,
            ("NAVALM-"+std::to_string(o.year)+(tex?"-practice.tex":"-practice.pdf")).c_str());
        if(!path.empty()) begin_book(o,path);
    }
    void chrono_guard() {
        if(!chrono_error.empty())throw std::runtime_error(chrono_error+" Reload the original log after correcting the file; it has not been overwritten.");
    }
    void chrono_refresh() {
        chrono_clock->clear();sight_clock->clear();int selected_index=-1;
        for(size_t i=0;i<clock_log.clocks.size();++i){
            const auto &c=clock_log.clocks[i];
            for(auto *p:{chrono_clock,sight_clock}){p->add("Clock");p->replace(int(i),c.name.c_str());}
            if(c.id==clock_log.selected)selected_index=int(i);
        }
        chrono_clock->value(selected_index);sight_clock->value(selected_index);
        chrono_apply_new->value(clock_log.apply_new);chrono_rows->clear();chrono_row_ids.clear();
        std::string summary=chrono_error;
        if(summary.empty() && !clock_log.selected)summary="Add a named watch or clock, then record its first UTC comparison. One reading gives offset; two distinct reference times also give drift.";
        if(summary.empty()){
            const auto &c=clock_log.clock(clock_log.selected);
            for(size_t i=0;i<clock_log.readings.size();++i){
                const auto &r=clock_log.readings[i];if(r.clock!=c.id)continue;
                std::ostringstream line;line<<(r.enabled?"  ":"x ")<<std::setw(5)<<r.series<<"  "<<chrono_nav::stamp(r.utc)<<"  "<<std::showpos<<std::fixed<<std::setprecision(3)<<std::setw(11)<<(r.watch-r.utc)<<std::noshowpos<<"   "<<r.source<<"  "<<r.note;
                chrono_rows->add(line.str().c_str());chrono_row_ids.push_back(i);
            }
            try{
                auto m=clock_log.model(c.id);summary=c.name+" · series "+std::to_string(c.series)+" · "+std::to_string(m.count)+" active comparisons\n";
                if(!m.count)summary+="No active comparisons in this series. Clock correction is unavailable.";
                else {
                    double current=double(std::time(nullptr)),error=m.error_at(current);
                    summary+="Estimated error now (watch − UTC): "+fmt(error,3)+" s · "+(m.count>1?"drift "+fmt(m.rate,4)+" s/day":"offset only; drift not established")+"\n";
                    summary+="Span "+fmt((m.last-m.first)/86400.,2)+" days · RMS residual "+fmt(m.rms,3)+" s (scatter, not guaranteed accuracy).";
                    if(current<m.first || current>m.last)summary+=" Prediction is outside the measured span.";
                }
            }catch(const std::exception &e){summary=e.what();}
        }
        chrono_summary->copy_label(summary.c_str());
        chrono_plot->set_data(clock_log,clock_log.selected,clock_log.selected?clock_log.clock(clock_log.selected).series:1);
    }
    size_t chrono_selected_index() const {
        int row=chrono_rows->value();if(row<1 || row>int(chrono_row_ids.size()))throw std::runtime_error("Select a comparison first.");
        return chrono_row_ids[row-1];
    }
    void chrono_select(size_t index) {
        auto row=std::find(chrono_row_ids.begin(),chrono_row_ids.end(),index);if(row==chrono_row_ids.end())return;
        int line=int(row-chrono_row_ids.begin())+1;chrono_rows->select(line);chrono_rows->middleline(line);
        const auto &r=clock_log.readings.at(index);chrono_plot->set_data(clock_log,r.clock,r.series,long(index));
        auto msg="Series "+std::to_string(r.series)+" · UTC "+chrono_nav::stamp(r.utc)+" · watch "+chrono_nav::stamp(r.watch)+" · error "+fmt(r.watch-r.utc,3)+" s";
        if(chrono_plot->model.count)msg+=" · residual "+fmt((r.watch-r.utc)-chrono_plot->model.error_at(r.utc),3)+" s";
        message(msg);
    }
    void chrono_cancel_edit(bool clear=false) {
        chrono_edit_index=-1;chrono_add_button->copy_label("Add comparison");chrono_cancel_button->deactivate();
        if(clear){chrono_ref_time->value("");chrono_watch_time->value("");chrono_note->value("");message("Edit cancelled. No comparison changed.");}
    }
    void chrono_begin_edit() {
        chrono_guard();auto index=chrono_selected_index();const auto &r=clock_log.readings.at(index);
        auto ref=chrono_nav::stamp(r.utc),watch=chrono_nav::stamp(r.watch);
        chrono_ref_date->value(ref.substr(0,10).c_str());chrono_ref_time->value(ref.substr(11).c_str());
        chrono_watch_date->value(watch.substr(0,10).c_str());chrono_watch_time->value(watch.substr(11).c_str());
        int source=0;for(;source<chrono_source->size()-1;++source)if(r.source==chrono_source->text(source))break;
        if(source==chrono_source->size()-1){chrono_source->add("Recorded source");chrono_source->replace(source,r.source.c_str());}
        chrono_source->value(source);chrono_note->value(r.note.c_str());
        chrono_edit_index=long(index);chrono_add_button->copy_label("Save changes");chrono_cancel_button->activate();
        message("Editing series "+std::to_string(r.series)+" comparison. Change fields above, then Save changes; clock, series and exclusion state are retained.");
    }
    void chrono_delete() {
        chrono_guard();auto index=chrono_selected_index();
        if(!testing && fl_choice("Delete this comparison from the log? Use Exclude instead to retain its history.","Cancel","Delete",nullptr)!=1)return;
        auto next=clock_log;next.erase(index);chrono_commit(next);message("Comparison deleted; the fit and plot have been updated.");
    }
    void chrono_load() {
        if(const char *path=std::getenv("NAVALM_CHRONO"))chrono_path=fs::u8path(path);
        else if(const char *path=std::getenv("NAVALMRC"))chrono_path=fs::u8path(path).parent_path()/"chrono.log";
        else chrono_path=application_root/".local/chrono.log";
        try{auto loaded=chrono_nav::Log::load(chrono_path);clock_log=std::move(loaded);chrono_error.clear();}
        catch(const std::exception &e){chrono_error=e.what();sight_clock_apply->value(0);}
        chrono_cancel_edit();chrono_refresh();
    }
    void chrono_commit(const chrono_nav::Log &next) {
        next.save(chrono_path);clock_log=next;chrono_cancel_edit();chrono_refresh();message("Chrono log saved: "+chrono_path.u8string());
    }
    void chrono_computer_now() {
        double utc=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto stamp=chrono_nav::stamp(utc);chrono_ref_date->value(stamp.substr(0,10).c_str());chrono_ref_time->value(stamp.substr(11).c_str());
        chrono_source->value(2);message("Computer UTC frozen. Pair it with the watch reading at that instant; computer accuracy and your reaction time affect this comparison.");
    }
    void chrono_add() {
        chrono_guard();chrono_nav::Reading r;r.clock=clock_log.selected;
        r.utc=chrono_nav::parse(chrono_ref_date->value(),chrono_ref_time->value());
        r.watch=chrono_nav::parse(chrono_watch_date->value(),chrono_watch_time->value());
        if(!chrono_source->text())throw std::runtime_error("Choose the reference source.");
        r.source=chrono_source->text();r.note=chrono_note->value();auto next=clock_log;
        size_t index=chrono_edit_index<0?next.readings.size():size_t(chrono_edit_index);
        if(chrono_edit_index<0)next.add(r);else next.update(index,r);
        chrono_commit(next);chrono_select(index);
    }
    void bind(Fl_Widget *w, std::function<void()> fn) {
        auto f = std::make_unique<std::function<void()>>([this, w, fn] {
            nav_set_desktop_action(w->label());
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
        if (error && !testing) {
#ifdef _WIN32
            nav_windows_error(s.c_str());
#else
            fl_alert("%s", s.c_str());
#endif
        }
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
        if(horizon->value()==2)short_distance->activate();else short_distance->deactivate();
        if (horizon->value() != 1) {
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
    void put_position(const Sight &s, Fl_Input *la, Fl_Choice *lah, Fl_Input *lo, Fl_Choice *loh) {
        la->value(angle_entry(s.dr_lat,2).c_str());lah->value(s.dr_lat<0);
        lo->value(angle_entry(s.dr_lon,3).c_str());loh->value(s.dr_lon<0);
    }
    void load_saved_position(Fl_Input *la, Fl_Choice *lah, Fl_Input *lo, Fl_Choice *loh, bool startup=false) {
        NavDefaults d;navrc_load(&d);
        if(!d.have_dr){
            if(startup){la->value("");lo->value("");return;}
            throw std::runtime_error("No saved DR. Enter it on Sight reduction and choose Save defaults.");
        }
        if(!std::isfinite(d.dr_lat) || !std::isfinite(d.dr_lon) || std::abs(d.dr_lat)>90 || std::abs(d.dr_lon)>180)
            throw std::runtime_error("Saved DR is invalid. Correct it on Sight reduction and save defaults again.");
        Sight s{};s.dr_lat=d.dr_lat;s.dr_lon=d.dr_lon;put_position(s,la,lah,lo,loh);
        if(!startup)message("Saved DR loaded. UTC and other page entries are unchanged; recalculate when ready.");
    }
    void copy_sight_position(Fl_Input *la, Fl_Choice *lah, Fl_Input *lo, Fl_Choice *loh) {
        Sight s{};coordinates(lat,lat_hemi,lon,lon_hemi,s);put_position(s,la,lah,lo,loh);
        message("Current sight DR copied. UTC and other page entries are unchanged; recalculate when ready.");
    }
    void coordinates(Fl_Input *la, Fl_Choice *lah, Fl_Input *lo, Fl_Choice *loh, Sight &s) {
        if (std::string(la->value()).find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error(
                "DR latitude is required. Enter degrees SPACE minutes and choose N or S.");
        if (std::string(lo->value()).find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error(
                "DR longitude is required. Enter degrees SPACE minutes and choose E or W.");
        s.dr_lat = angle(la->value(), "DR latitude", 90) * (lah->value() ? -1 : 1);
        s.dr_lon = angle(lo->value(), "DR longitude", 180) * (loh->value() ? -1 : 1);
    }
    Sight read_sight(bool require_hs=true) {
        if (body->value() < 0 || body->value() > 7)
            throw std::runtime_error("Choose an observed body.");
        Sight s{};
        if(require_hs && sight_clock_apply->value())
            date_time(sight_watch_date->value(),sight_watch_time->value(),s);
        else date_time(date->value(), !require_hs && !*time->value()?"00:00:00":time->value(), s);
        const char *b[] = {"sun", "moon", "mercury", "venus", "mars", "jupiter", "saturn", "star"};
        std::snprintf(s.body, sizeof s.body, "%s", b[body->value()]);
        if (body->value() == 7)
            std::snprintf(s.star, sizeof s.star, "%s", nav_star_name(star->value()));
        s.artificial = horizon->value()==1;
        s.short_horizon_nm = horizon->value()==2 ? number(short_distance->value(),"Waterline distance in NM",.000001,1000) : 0;
        s.hs_deg = require_hs ? angle(hs->value(), "Sextant altitude", s.artificial ? 180 : 90) : 0;
        s.ie_min = number(ie->value(), "Index error", 0, 120);
        s.ie_on_arc = arc->value();
        s.he_metres = units->value();
        s.he = s.artificial ? 0 : number(height->value(), "Height of eye", 0, 10000);
        s.limb = 1 - limb->value();
        s.temp_c = number(temp->value(), "Temperature", -100, 100);
        s.press_mb = number(press->value(), "Pressure", 0, 1200);
        coordinates(lat, lat_hemi, lon, lon_hemi, s);
        s.reduce_at_dr = at_dr->value();
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
        if(require_hs && sight_clock_apply->value()){
            chrono_guard();int i=sight_clock->value();
            if(i<0 || i>=int(clock_log.clocks.size()))throw std::runtime_error("Select a calibrated Chrono clock first.");
            const auto &c=clock_log.clocks[i];chrono_nav::correct(s,c,clock_log.model(c.id));
        } else if(clock_snapshot.clock_name[0] && std::abs(navsight_jd(&s)-navsight_jd(&clock_snapshot))*86400.<.0005){
            std::snprintf(s.clock_name,sizeof s.clock_name,"%s",clock_snapshot.clock_name);
            std::snprintf(s.clock_watch,sizeof s.clock_watch,"%s",clock_snapshot.clock_watch);
            s.clock_correction_sec=clock_snapshot.clock_correction_sec;
        }
        return s;
    }
    void fill_sight(const Sight &s) {
        int index = navsight_body_index(s.body);
        if (std::strcmp(s.body, "star") && (index < 0 || index > 6))
            throw std::runtime_error("Saved sight has an unknown body.");
        edit_id = s.id;
        clock_snapshot=s;sight_clock_apply->value(0);
        sight_clock_status->copy_label(s.clock_name[0]?("Saved UTC already includes "+std::string(s.clock_name)+" correction "+fmt(s.clock_correction_sec,3)+" s; Chrono is off to prevent applying it twice.").c_str():"Chrono is off; the entered time is UTC.");
        body->value(!std::strcmp(s.body, "star") ? 7 : navsight_body_index(s.body));
        int idx = nav_star_find(s.star);
        if (idx >= 0)
            star->value(idx);
        char b[64];
        std::snprintf(b, sizeof b, "%04d-%02d-%02d", s.y, s.mo, s.d);
        date->value(b);
        sight_watch_date->value(s.clock_watch[0]?std::string(s.clock_watch,10).c_str():b);
        std::snprintf(b, sizeof b, "%02d:%02d:%06.3f", s.h, s.mi, s.s);
        time->value(b);
        sight_watch_time->value(std::strlen(s.clock_watch)>11?s.clock_watch+11:"");
        hs->value(std::isfinite(s.hs_deg) ? angle_entry(s.hs_deg, 2).c_str() : "");
        label->value(s.label);
        lat->value(std::isfinite(s.dr_lat) ? angle_entry(s.dr_lat, 2).c_str() : "");
        lon->value(std::isfinite(s.dr_lon) ? angle_entry(s.dr_lon, 3).c_str() : "");
        lat_hemi->value(s.dr_lat < 0);
        lon_hemi->value(s.dr_lon < 0);
        horizon->value(s.artificial?1:s.short_horizon_nm>0?2:0);
        short_distance->value(s.short_horizon_nm>0?fmt(s.short_horizon_nm,6).c_str():"");
        limb->value(1 - s.limb);
        ie->value(fmt(s.ie_min).c_str());
        arc->value(s.ie_on_arc);
        height->value(fmt(s.he).c_str());
        units->value(s.he_metres);
        temp->value(fmt(s.temp_c).c_str());
        press->value(fmt(s.press_mb).c_str());
        at_dr->value(s.reduce_at_dr);
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
        fill_sight(s);hs->value("");
        if(clock_log.apply_new && chrono_error.empty()){
            sight_clock_apply->value(1);time->value("");
            sight_clock_status->copy_label("Enter the separate watch date/time; Chrono will fill UTC when reducing.");
        }
        if (!d.have_dr) {
            lat->value("");
            lon->value("");
        }
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
        if(s.clock_name[0]){
            auto utc=chrono_nav::stamp(chrono_nav::epoch(s));
            date->value(utc.substr(0,10).c_str());time->value(utc.substr(11).c_str());
        }
        sight_clock_status->copy_label(s.clock_name[0]?("UTC "+chrono_nav::stamp(chrono_nav::epoch(s))+" · "+s.clock_name+" correction "+fmt(s.clock_correction_sec,3)+" s (added once).").c_str():"Chrono is off; the entered time is UTC.");
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
        report_buffer.text("");
        sight_clock_status->copy_label(sight_clock_apply->value()?"Chrono correction pending valid inputs.":"Entered time is UTC; checking sight inputs.");
        headline->copy_label("Check the sight inputs");
        auto s = read_sight();
        auto r = reduce(s);
        show_reduction(s, r);
        message("Sight reduced with " + std::string(nav_engine_name()) +
                ". Check the worked report before saving.");
    }
    void clear_fix() {
        show_cluster(false);
        cluster_ids.clear();
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
            if(old->cluster_used){
                navsight_store_free(&candidate);
                throw std::runtime_error("A representative LOP has no raw sight to edit.");
            }
            s.id=old->id;
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
        const auto &s=store.v[rows[0]];
        if(s.cluster_used){
            fix_buffer.text(capture([&](FILE *p){
                auto r=reduce(s);
                navsight_print_correction(p,&s,&r);
                navsight_print_reduction(p,&s,&r);
            }).c_str());
            message("Representative LOP #"+std::to_string(s.id)+" shown in the report; no raw Hs to edit.");
            return;
        }
        fill_sight(s);
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
        if(navsight_store_load(&next, err, sizeof err))
            throw std::runtime_error(err);
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
    void show_cluster(bool show) {
        if(show && expanded_window && expanded_window->shown()) reattach_plot();
        showing_cluster=show;
        for(auto *w:chart_controls) { if(show)w->hide(); else w->show(); }
        for(auto *w:cluster_controls) { if(show)w->show(); else w->hide(); }
        if(show){ plot->hide(); detached_hint->hide(); }
        else if(expanded_window && expanded_window->shown()){plot->hide();detached_hint->show();}
        else {plot->show();detached_hint->hide();}
        fix_tab->redraw();
    }
    void preview_cluster() {
        cluster_ids.clear();
        cluster_save_button->deactivate();
        cluster_replace_button->deactivate();
        std::vector<Sight> inputs;
        for(int row:selected())inputs.push_back(store.v[row]);
        NavSightCluster result{};
        char err[512]{};
        if(navsight_cluster(inputs.data(),inputs.size(),cluster_reject->value(),&result,err,sizeof err))
            throw std::runtime_error(err);
        cluster=result;
        const Sight &ref=cluster.sight;
        auto jd=[](const Sight &s){return navsight_jd(&s);};
        auto earliest=std::min_element(inputs.begin(),inputs.end(),[&](const Sight &a,const Sight &b){return jd(a)<jd(b);});
        const double origin=jd(*earliest);
        cluster_plot->epoch=chrono_nav::stamp(chrono_nav::epoch(*earliest));
        cluster_plot->ref_sec=(jd(ref)-origin)*86400;
        cluster_plot->ref_ho=ref.cluster_ho;
        cluster_plot->caption=std::string(!std::strcmp(ref.body,"star")?ref.star:ref.body)+" · Corrected altitude (Ho) vs. time";
        cluster_plot->points.clear();
        for(size_t i=0;i<inputs.size();i++){
            cluster_ids.push_back(inputs[i].id);
            cluster_plot->points.push_back({(jd(inputs[i])-origin)*86400,result.ho_deg[i],
                result.hc_deg[i]+result.offset_min/60,inputs[i].id,result.keep[i]!=0});
        }
        std::stable_sort(cluster_plot->points.begin(),cluster_plot->points.end(),
                         [](const auto &a,const auto &b){return a.sec<b.sec;});
        auto summary=std::to_string(ref.cluster_used)+"/"+std::to_string(ref.cluster_total)+
            " used · scatter "+fmt(ref.cluster_sd_min,2)+"′ SD · representative "+
            chrono_nav::stamp(chrono_nav::epoch(ref)).substr(11)+" UTC";
        cluster_summary->copy_label((summary+"\nFor a fix, select the representative or its source sights.").c_str());
        cluster_save_button->activate();
        cluster_replace_button->activate();
        show_cluster(true);
        cluster_plot->redraw();
        message("Cluster preview. Review the excluded points before saving.");
    }
    struct StoreGuard {
        SightStore data{};
        ~StoreGuard(){ navsight_store_free(&data); }
    };
    void adopt_store(StoreGuard &next) {
        navsight_store_free(&store);
        store=next.data;
        next.data={};
        edit_id=0;
        update_button->deactivate();
        refresh();
        clear_fix();
    }
    void save_cluster(bool replace) {
        if(cluster_ids.empty())throw std::runtime_error("Plot a cluster before saving.");
        std::vector<int> now;
        for(int row:selected())now.push_back(store.v[row].id);
        if(now!=cluster_ids)throw std::runtime_error("Selection changed. Plot the cluster again before saving.");
        if(replace && !testing &&
           fl_choice("Replace all %zu selected observations, including any excluded points, with one representative LOP?",
                     "Cancel","Replace",nullptr,cluster_ids.size())!=1)return;
        StoreGuard next;
        char err[512]{};
        if(navsight_store_load(&next.data,err,sizeof err))throw std::runtime_error(err);
        for(int id:cluster_ids){
            auto *current=navsight_store_find(&next.data,id);
            auto *previewed=navsight_store_find(&store,id);
            if(!current || !previewed || std::memcmp(current,previewed,sizeof(Sight)))
                throw std::runtime_error("A source sight changed on disk. Reload the log and plot the cluster again.");
        }
        const int id=navsight_store_add(&next.data,&cluster.sight);
        if(!id)throw std::runtime_error("Could not add representative LOP.");
        if(replace)for(int source:cluster_ids)navsight_store_delete(&next.data,source);
        if(navsight_store_save(&next.data,err,sizeof err))throw std::runtime_error(err);
        adopt_store(next);
        for(size_t i=0;i<store.n;i++)if(store.v[i].id==id)saved->select((int)i+1);
        cluster_save_button->deactivate();
        cluster_replace_button->deactivate();
        message("Saved representative LOP #"+std::to_string(id)+
                (replace?"; selected originals replaced.":"; originals kept."));
    }
    void export_sight_log(const std::string &path) {
        auto dest=fs::u8path(path), live=fs::u8path(navsight_store_path());
        std::error_code ec;
        if(fs::equivalent(dest,live,ec) || fs::absolute(dest).lexically_normal()==fs::absolute(live).lexically_normal())
            throw std::runtime_error("Choose a separate file for the backup.");
        StoreGuard current;
        char err[512]{};
        if(navsight_store_load(&current.data,err,sizeof err) ||
           navsight_store_export(&current.data,path.c_str(),err,sizeof err))throw std::runtime_error(err);
        message("Backed up "+std::to_string(current.data.n)+" saved sights.");
    }
    void backup_sights() {
        auto path=choose_file("Back up all saved sights","NavAlm sight backup\t*.save",true,"navalm-sights-backup.save");
        if(!path.empty())export_sight_log(path);
    }
    void import_sight_log(const std::string &path) {
        StoreGuard next,current;
        char err[512]{};
        if(navsight_store_import(&next.data,path.c_str(),err,sizeof err) ||
           navsight_store_load(&current.data,err,sizeof err))throw std::runtime_error(err);
        if(current.data.n && !testing &&
           fl_choice("Replace the %zu sights in this log with %zu sights from the backup?\nBack up this log first if you want to keep it.",
                     "Cancel","Restore backup",nullptr,current.data.n,next.data.n)!=1)return;
        if(navsight_store_save(&next.data,err,sizeof err))throw std::runtime_error(err);
        adopt_store(next);
        message("Restored "+std::to_string(store.n)+" saved sights from backup.");
    }
    void restore_sights() {
        auto path=choose_file("Restore a saved sights backup","NavAlm sight backup\t*.save",false);
        if(!path.empty())import_sight_log(path);
    }
    void defaults_state() {
        if(def_horizon->value()==2)def_short->activate();else def_short->deactivate();
        if(def_horizon->value()!=1){def_height->activate();def_units->activate();}
        else {def_height->deactivate();def_units->deactivate();}
        for(auto *field:{def_course,def_speed,def_set,def_drift})
            if(def_motion->value())field->activate();else field->deactivate();
    }
    void fill_defaults_from_sight(const Sight &s,bool have_dr) {
        if(have_dr)put_position(s,def_lat,def_lat_hemi,def_lon,def_lon_hemi);
        else {def_lat->value("");def_lon->value("");}
        def_horizon->value(s.artificial?1:s.short_horizon_nm>0?2:0);
        def_short->value(s.short_horizon_nm>0?fmt(s.short_horizon_nm,6).c_str():"");
        def_height->value(fmt(s.he).c_str());def_units->value(s.he_metres);
        def_limb->value(1-s.limb);def_ie->value(fmt(s.ie_min).c_str());def_arc->value(s.ie_on_arc);
        def_temp->value(fmt(s.temp_c).c_str());def_press->value(fmt(s.press_mb).c_str());
        def_at_dr->value(s.reduce_at_dr);def_motion->value(s.has_motion);
        def_course->value(fmt(s.course).c_str());def_speed->value(fmt(s.speed).c_str());
        def_set->value(fmt(s.set).c_str());def_drift->value(fmt(s.drift).c_str());defaults_state();
    }
    void fill_defaults() {
        NavDefaults d;navrc_load(&d);Sight s{};navrc_apply(&d,&s);
        fill_defaults_from_sight(s,d.have_dr);
    }
    void save_setup_defaults() {
        Sight s{};std::strcpy(s.body,"sun");s.y=2026;s.mo=s.d=1;s.hs_deg=45;
        bool have_dr=std::string(def_lat->value()).find_first_not_of(" \t")!=std::string::npos ||
                     std::string(def_lon->value()).find_first_not_of(" \t")!=std::string::npos;
        if(have_dr)coordinates(def_lat,def_lat_hemi,def_lon,def_lon_hemi,s);
        s.ie_min=number(def_ie->value(),"Index error",0,120);s.ie_on_arc=def_arc->value();
        s.artificial=def_horizon->value()==1;
        s.he=number(def_height->value(),"Height of eye",0,10000);s.he_metres=def_units->value();
        s.short_horizon_nm=def_horizon->value()==2?number(def_short->value(),"Waterline distance in NM",.000001,1000):0;
        s.limb=1-def_limb->value();s.temp_c=number(def_temp->value(),"Temperature",-100,100);
        s.press_mb=number(def_press->value(),"Pressure",0,1200);s.reduce_at_dr=def_at_dr->value();
        s.has_motion=def_motion->value();
        if(s.has_motion){
            s.course=number(def_course->value(),"Course",0,360);s.speed=number(def_speed->value(),"Speed",0,100);
            s.set=number(def_set->value(),"Set",0,360);s.drift=number(def_drift->value(),"Drift",0,100);
        }
        char err[512]{};if(navsight_validate(&s,err,sizeof err))throw std::runtime_error(err);
        NavDefaults d;navrc_init(&d);navrc_from_sight(&d,&s);d.have_dr=have_dr;
        if(navrc_save(&d,err,sizeof err))throw std::runtime_error(err);
        fill_defaults();message("Defaults saved. New sights use them; use the DR load buttons to update existing pages.");
    }
    void save_defaults() {
        auto s = read_sight(false);
        NavDefaults d;
        navrc_init(&d);
        navrc_from_sight(&d, &s);
        char err[512]{};
        if(navsight_validate(&s,err,sizeof err))throw std::runtime_error(err);
        if (navrc_save(&d, err, sizeof err))
            throw std::runtime_error(err);
        // Hs is not needed to save instrument/DR settings.
        fill_defaults();
        message("Saved input defaults to " + std::string(navrc_path()));
    }
    Sight almanac_input() {
        Sight s{};
        date_time(al_date->value(),al_time->value(),s);
        coordinates(al_lat,al_lat_hemi,al_lon,al_lon_hemi,s);
        check_engine(s);
        int b=al_body->value();
        if(b<0 || b>8)throw std::runtime_error("Choose an almanac object.");
        const char *names[]={"sun","moon","mercury","venus","mars","jupiter","saturn","aries","star"};
        std::snprintf(s.body,sizeof s.body,"%s",names[b]);
        if(b==8)std::snprintf(s.star,sizeof s.star,"%s",nav_star_name(al_star->value()));
        return s;
    }
    void almanac_to_sight() {
        auto at=almanac_input();
        if(!std::strcmp(at.body,"aries"))
            throw std::runtime_error("Aries is a reference point. Choose a star, Sun, Moon or planet for a sight.");
        bool direct=at_dr->value();
        new_sight();sight_clock_apply->value(0);body->value(al_body->value()==8?7:al_body->value());
        sight_clock_status->copy_label("Almanac time is already UTC; Chrono is off.");
        if(al_body->value()==8)star->value(al_star->value());
        else if(body->value()>=2)limb->value(1);
        date->value(al_date->value());time->value(al_time->value());
        put_position(at,lat,lat_hemi,lon,lon_hemi);at_dr->value(direct);
        hs->value("");body_state();tabs->value(sight_tab);hs->take_focus();
        message("Almanac object, UTC and DR copied to a new sight. Enter measured Hs; check your instrument settings.");
    }
    void almanac(bool hourly) {
        auto s=almanac_input();
        int b=al_body->value();
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
    void estimate_index() {
        auto rows = selected();
        if (rows.size() < 3)
            throw std::runtime_error("Select at least three saved sights from the same sextant.");
        std::vector<NavIndexCheck> checks;
        std::vector<double> required, additional;
        double saved_mean = 0, zero_mean = 0, saved_sq = 0, zero_sq = 0;
        for (int row : rows) {
            const auto &s = store.v[row];
            NavIndexCheck c{};
            char err[512]{};
            if (navsight_index_check(&s, &c, err, sizeof err))
                throw std::runtime_error("Saved sight #" + std::to_string(s.id) + ": " + err);
            checks.push_back(c);
            required.push_back(c.required_ic_min);
            additional.push_back(c.required_ic_min - c.saved_ic_min);
            saved_mean += c.residual_saved_min;
            zero_mean += c.residual_zero_min;
        }
        saved_mean /= checks.size();
        zero_mean /= checks.size();
        for (const auto &c : checks) {
            saved_sq += std::pow(c.residual_saved_min - saved_mean, 2);
            zero_sq += std::pow(c.residual_zero_min - zero_mean, 2);
        }
        auto median = [](std::vector<double> &v) {
            std::sort(v.begin(), v.end());
            return (v[v.size()/2] + v[(v.size()-1)/2]) / 2;
        };
        double ic = median(required);
        double extra = median(additional);
        auto report = capture([&](FILE *p) {
            std::fprintf(p, "%zu selected sights · %s engine\n"
                "Reference: each sight's saved DR and UTC, without AP or table rounding.\n"
                "Use independently known positions and real sights from one sextant.\n\n",
                checks.size(), nav_engine_name());
            if (std::fabs(ic) < .005)
                std::fprintf(p, "Estimated index error: 0.00'\n");
            else
                std::fprintf(p, "Estimated index error: %.2f' %s the arc\n", std::fabs(ic),
                             ic < 0 ? "on" : "off");
            std::fprintf(p, "Median total correction to raw Hs: %+.2f'\n"
                "Median change from the saved corrections: %+.2f'\n\n"
                "Altitude residual (Ho - Hc)       Mean       Scatter (SD)\n"
                "With saved index correction    %+7.2f'       %7.2f'\n"
                "With zero index correction     %+7.2f'       %7.2f'\n\n"
                "Positive residual means the reduced altitude is too high.\n"
                "A negative correction subtracts from raw Hs before any AH halving.\n"
                "Position, clock, horizon and observer errors can mimic index error.\n"
                "Compare with a direct index check; no sights or defaults were changed.\n\n"
                "  Sight  Object         Saved IC  Needed IC    As saved     Zero IC\n",
                ic, extra, saved_mean, std::sqrt(saved_sq/(checks.size()-1)),
                zero_mean, std::sqrt(zero_sq/(checks.size()-1)));
            for (size_t i = 0; i < rows.size(); ++i) {
                const auto &s = store.v[rows[i]];
                const auto &c = checks[i];
                std::fprintf(p, " %6d  %-13.13s  %+7.2f'   %+7.2f'    %+7.2f'    %+7.2f'\n",
                    s.id, !std::strcmp(s.body, "star") ? s.star : s.body, c.saved_ic_min,
                    c.required_ic_min, c.residual_saved_min, c.residual_zero_min);
            }
        });
        index_buffer.text(report.c_str());
        if (!testing) {
            if (!index_window) {
                index_window = std::make_unique<Fl_Double_Window>(740, 525, "NavAlm — Index error estimate");
                index_window->begin();
                auto *output = display(16, 16, 708, 449, index_buffer);
                button(16, 478, 170, "Export report…", [this] {
                    export_text(index_buffer, "index-error.txt");
                });
                button(624, 478, 100, "Close", [this] { index_window->hide(); });
                index_window->resizable(output);
                index_window->size_range(640, 390);
                index_window->end();
            }
            index_window->show();
        }
        message("Index error estimated from " + std::to_string(checks.size()) + " selected sights.");
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
            try {
                r.push_back(reduce(s.back()));
            } catch (const std::exception &e) {
                throw std::runtime_error("Saved sight #" + std::to_string(s.back().id) + ": " +
                                         e.what() +
                                         "\nSelect it and choose Edit selected to correct it.");
            }
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
                std::make_unique<Fl_Double_Window>(800, 830, "NavAlm — Universal Plotting Sheet");
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
        lunar->invalidate();
        clear_fix();
        report_buffer.text("");
        headline->copy_label("Engine changed — reduce the sight again.");
        al_buffer.text("");
        invalidate_sky();
        engine_info();
        message("Using " + std::string(nav_engine_name()));
    }
    void choose_ephemeris() {
        lunar->invalidate();
        auto path = choose_file("Select repacked JPL ephemeris", "NAVALM ephemeris\t*.nav", false);
        if (path.empty())
            return;
        invalidate_sky();
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
static int chrono_self_test(bool preview=false);
static int lunar_self_test(bool preview=false);
int main(int argc, char **argv) {
    bool checks = std::string(argv[0]).find("navalm-checks") != std::string::npos;
    bool chrono_checks=std::getenv("NAVALM_CHRONO_CHECK")!=nullptr;
    bool lunar_checks=std::getenv("NAVALM_LUNAR_CHECK")!=nullptr;
    bool all_checks=std::getenv("NAVALM_ALL_CHECKS")!=nullptr;
    checks=checks || chrono_checks || lunar_checks || all_checks;
    try {
        auto root = runtime_root(argv[0]);
        application_root = root;
        application_resources = fs::absolute(fs::u8path(argv[0])).parent_path() / "../Resources";
        fs::create_directories(root / ".local");
        nav_enable_diagnostics(root / ".local");
#ifdef NAVALM_EMBEDDED_DATA
        auto ephemeris_target = root / ".local/ephem/de440s.nav";
        auto map_target = root / ".local/maps/ne_10m_land.shp";
        if (!fs::exists(ephemeris_target) || !fs::exists(map_target)) {
            Fl_Double_Window preparing(520, 170, "Preparing NavAlm — first launch");
            Fl_Box note(20, 18, 480, 56,
                        "Preparing bundled offline data.\nNo download is required.");
            note.labelsize(14);
            Fl_Progress progress(24, 110, 472, 30);
            progress.minimum(0);
            progress.maximum(1);
            progress.labelsize(12);
            preparing.end();
            preparing.show();
            Fl::check();
            for (auto item : {std::pair<int, fs::path>{901, ephemeris_target}, {902, map_target}}) {
                if (fs::exists(item.second))
                    continue;
                note.copy_label(
                    item.first == 901
                        ? "Preparing the precise JPL ephemeris…\nFrom the bundled data; no "
                          "download."
                        : "Preparing the offline world map…\nFrom the bundled data; no download.");
                nav_unpack_resource(item.first, item.second, [&](size_t done, size_t total) {
                    progress.value(total ? double(done) / total : 0);
                    progress.copy_label(
                        (fmt(done / 1048576., 1) + " / " + fmt(total / 1048576., 1) + " MB")
                            .c_str());
                    preparing.redraw();
                    Fl::check();
                });
            }
            preparing.hide();
        }
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
        if(all_checks || (argc>1&&!std::strcmp(argv[1],"--all-self-tests"))){
            nav_setenv("NAVALM_TEST_DOWNLOAD","1",1);self_test();
            nav_engine_select("precise");nav_engine_init(nullptr,0);chrono_self_test(false);
            int code=lunar_self_test(all_checks);nav_engine_shutdown();return code;
        }
        if(lunar_checks || (argc>1 && !std::strcmp(argv[1],"--lunar-self-test"))){
            int code=lunar_self_test(lunar_checks);nav_engine_shutdown();return code;
        }
        if(chrono_checks || (argc>1 && !std::strcmp(argv[1],"--chrono-self-test"))){
            int code=chrono_self_test(chrono_checks);nav_engine_shutdown();return code;
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
        const char *start_tab=std::getenv("NAVALM_START_TAB");
        if((argc>1 && !std::strcmp(argv[1],"--chrono")) || (start_tab && !std::strcmp(start_tab,"chrono")))
            app.tabs->value(app.chrono_tab);
        if((argc>1 && !std::strcmp(argv[1],"--lunars")) || (start_tab && !std::strcmp(start_tab,"lunars")))
            app.tabs->value(app.lunar);
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

static int lunar_self_test(bool preview) {
    auto require=[](bool v,const char *s){if(!v)throw std::runtime_error(std::string("LUNAR UI TEST: ")+s);};
    auto tmp=nav_temp_dir();
    struct Cleanup {fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{tmp};
    nav_setenv("NAVALM_CHRONO",(tmp/"chrono.log").u8string().c_str(),1);
    nav_setenv("NAVALMRC",(tmp/"defaults.rc").u8string().c_str(),1);
    nav_setenv("NAVALM_SAVE",(tmp/"sights.save").u8string().c_str(),1);
    Workbench app;app.testing=true;auto &p=*app.lunar;
    require(app.tabs->find(app.chrono_tab)==app.tabs->find(app.lunar)+1,"Lunars immediately before Chrono");
    require(p.moon_contact->value()==0,"new Moon contact defaults to Near");
    p.example();app.tabs->value(app.lunar);app.win.show();Fl::check();p.solve();
    require(p.ready&&p.result.count==1,"example solves through native inputs");
    require(std::abs(p.result.solutions[0].correction_sec-66.02)<.1,"example recovered UTC");
    std::string raw_date=p.date->value(),raw_time=p.time->value(),raw_distance=p.distance->value();
    p.initial->value("50");p.initial->do_callback();require(!p.ready&&!p.send->active()&&!p.export_button->active(),"edit invalidates cached result and exports");p.solve();
    require(std::abs(p.result.solutions[0].correction_sec-66.02)<.1,"prior correction applied once");
    p.altitude[0].hs->value("50 12.3");p.altitude[0].time->value("13:19:00.123");
    p.altitude[0].measured->value(1);p.altitude[0].measured->do_callback();
    require(!p.refine->active(),"one real altitude cannot refine position");
    p.altitude[0].measured->value(0);p.altitude[0].measured->do_callback();
    require(std::string(p.altitude[0].hs->value())=="50 12.3"&&std::string(p.altitude[0].time->value())=="13:19:00.123","switch preserves original Hs and epoch");
    p.solve();require(p.saved.moon.measured==0&&p.result.solutions[0].lat==34,"synthetic stored reading ignored");
    require(raw_date==p.date->value()&&raw_time==p.time->value()&&raw_distance==p.distance->value(),"solving never overwrites raw observation");
    auto q=p.saved;q.moon.measured=q.other.measured=1;
    chrono_nav::set_time(q.moon.sight,chrono_nav::epoch(q.distance)-90.125);
    chrono_nav::set_time(q.other.sight,chrono_nav::epoch(q.distance)+60.5);
    q.moon.sight.ie_min=1.2;q.moon.sight.temp_c=15;q.moon.sight.press_mb=1000;
    NavLunarSolution truth;char e[512];require(!navlunar_predict(&q,75,34,-117,&truth,e,sizeof e),"real-shot fixture");
    q.distance.hs_deg=truth.raw_distance_deg;q.moon.sight.hs_deg=truth.predicted_hs[0];q.other.sight.hs_deg=truth.predicted_hs[1];q.refine_position=1;
    p.fill(q);auto read=p.read();require(read.moon.sight.ie_min==1.2&&read.moon.sight.temp_c==15&&read.moon.sight.press_mb==1000,"per-altitude settings survive load/read");
    p.solve();require(p.ready&&std::abs(p.result.solutions[0].correction_sec-75)<.1,"real own-epoch position/time fit");
    require(p.result.count>=2,"alternative real-altitude branch retained");
    chrono_nav::Log clocks;int id=clocks.add_clock("Lunar test watch");double t=chrono_nav::epoch(q.distance);
    clocks.add({id,1,t,t-20,true,"Manual WWV","Test only"});app.chrono_commit(clocks);
    p.example();p.solve();p.send->do_callback();require(app.tabs->value()==app.chrono_tab,"Chrono transfer switches tabs");
    require(app.clock_log.readings.size()==1,"prepare does not auto-save reference");
    double watch=chrono_nav::parse(app.chrono_watch_date->value(),app.chrono_watch_time->value());
    double utc=chrono_nav::parse(app.chrono_ref_date->value(),app.chrono_ref_time->value());
    require(std::abs((utc-watch)-p.result.solutions[0].correction_sec)<.002,"Chrono retains raw watch and separate derived UTC");
    p.initial->value("0");double estimate=p.chrono_estimate(watch);require(std::abs(estimate-20)<.001,"Chrono inverse sign");
    app.engine->value(1);app.change_engine();require(!p.ready,"engine change invalidates lunar result");
    bool rejected=false;try{p.solve();}catch(const std::exception&){rejected=true;}require(rejected,"Classic fails politely");
    app.engine->value(0);app.change_engine();require(std::strstr(nav_engine_detail(),"NOVAS")!=nullptr,"engine audit updates after switching back");p.example();p.solve();app.tabs->value(app.lunar);app.win.redraw();Fl::check();
    std::puts("PASS native Lunars: tab order, fixed AP, raw-data preservation, input invalidation, one-prior-only, own epochs/settings, two measured branches, Chrono preparation and engine guard.");
    if(preview){app.testing=false;app.message("Lunar checks passed · fictional example · " NAVALM_VERSION "");Fl::run();}else app.win.hide();
    return 0;
}

static int chrono_self_test(bool preview) {
    auto require=[](bool v,const char *s){if(!v)throw std::runtime_error(std::string("CHRONO TEST: ")+s);};
    auto tmp=nav_temp_dir();
    struct Cleanup {fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{tmp};
    nav_setenv("NAVALM_CHRONO",(tmp/"chrono.log").u8string().c_str(),1);
    nav_setenv("NAVALMRC",(tmp/"defaults.rc").u8string().c_str(),1);
    nav_setenv("NAVALM_SAVE",(tmp/"sights.save").u8string().c_str(),1);
    Workbench app;app.testing=true;
    chrono_nav::Log fixture;int id=fixture.add_clock("Example watch");double t=chrono_nav::parse("2026-10-28","12:00:00");
    const double errors[]={10,11.9,35,16.1,18};
    for(int i=0;i<5;++i)fixture.add({id,1,t+i*86400.,t+i*86400.+errors[i],true,"Manual WWV","Synthetic comparison"});
    app.chrono_commit(fixture);app.tabs->value(app.chrono_tab);app.win.show();Fl::check();
    require(app.chrono_plot->points.size()==5 && app.chrono_plot->model.count==5,"scatter and fitted line use active series");
    const auto &point=app.chrono_plot->points[2];auto index=app.chrono_plot->hit(app.chrono_plot->px(point.utc),app.chrono_plot->py(point.error));
    require(index==2 && app.chrono_plot->hit(-100,-100)<0,"plot hit-test finds outlier without selecting empty space");
    app.chrono_plot->select(size_t(index));require(app.chrono_selected_index()==2 && app.chrono_plot->selected==2,"point selection highlights matching log row");
    app.chrono_exclude_button->do_callback();
    require(app.chrono_plot->points.size()==5 && !app.chrono_plot->points[2].enabled && app.chrono_plot->model.count==4,"excluded point remains visible and leaves the fit");
    app.chrono_edit_button->do_callback();require(app.chrono_edit_index==2 && std::string(app.chrono_watch_time->value())=="12:00:35.000","edit loads original watch reading");
    app.chrono_watch_time->value("not a time");app.chrono_add_button->do_callback();
    require(app.chrono_edit_index==2 && app.clock_log.readings[2].watch==fixture.readings[2].watch,"invalid edit keeps original row and editor open");
    app.chrono_watch_time->value("12:00:14.000");app.chrono_note->value("Transcription corrected");app.chrono_add_button->do_callback();
    require(app.clock_log.readings.size()==5 && !app.clock_log.readings[2].enabled && app.clock_log.readings[2].note=="Transcription corrected" && app.chrono_edit_index<0,"save replaces row while preserving exclusion");
    app.chrono_exclude_button->do_callback();require(app.chrono_plot->model.count==5 && std::abs(app.chrono_plot->model.rate-2.02)<1e-6,"restoring corrected point refits drift");
    app.chrono_edit_button->do_callback();app.chrono_watch_time->value("12:00:59");app.chrono_cancel_button->do_callback();
    require(app.clock_log.readings[2].watch==t+2*86400.+14,"cancel does not change comparison");
    app.chrono_select(2);app.chrono_delete_button->do_callback();app.chrono_load();
    require(app.clock_log.readings.size()==4 && app.chrono_plot->points.size()==4 && app.chrono_plot->model.count==4,"deletion persists and updates plot and model");
    auto history=app.clock_log;history.clock(id).series++;app.chrono_commit(history);app.chrono_select(0);app.chrono_edit_button->do_callback();
    app.chrono_note->value("Earlier series note");app.chrono_add_button->do_callback();
    require(app.clock_log.readings[0].series==1 && app.clock_log.model(id).count==0 && app.chrono_plot->series==1,"historical edit and plot do not populate current model");
    while(!app.clock_log.readings.empty()){app.chrono_select(0);app.chrono_delete_button->do_callback();}
    require(app.chrono_plot->points.empty() && !app.chrono_plot->model.count,"empty plot after deleting final row");
    fixture.set_enabled(2,false);app.chrono_commit(fixture);app.chrono_select(2);app.win.redraw();Fl::check();
    std::puts("PASS native Chrono scatter: selection, excluded outlier, fit, validation, edit/cancel, deletion/reload, old series and empty plot; synthetic data only.");
    if(preview){app.testing=false;app.message("Chrono checks passed · synthetic example data only");Fl::run();}
    else app.win.hide();
    return 0;
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
    // Exercise the reported workflow through the real reduction callback and redraw.
    Sight altair{};
    std::strcpy(altair.body,"star");std::strcpy(altair.star,"Altair");
    altair.y=2026;altair.mo=11;altair.d=1;altair.h=22;altair.mi=6;altair.s=15;
    altair.dr_lat=34+2.5/60;altair.dr_lon=-(117+21.9/60);
    altair.hs_deg=43+38./60;altair.temp_c=10;altair.press_mb=1010;
    altair.reduce_at_dr=1;
    app.tabs->value(app.sight_tab);
    for(const char *star_name:{"Altair","Arcturus"}){
    std::snprintf(altair.star,sizeof altair.star,"%s",star_name);app.fill_sight(altair);
    for(int horizon_mode=0;horizon_mode<3;++horizon_mode){
        app.horizon->value(horizon_mode);app.height->value("10");app.short_distance->value("0.5");
        app.hs->value(horizon_mode==1?"87 16":"43 38");
        for(int i=0;i<240;++i){
            app.ie->value(fmt((i%121)*.1,1).c_str());app.arc->value(i%2);
            app.reduce_button->do_callback();Fl::check();
            require(std::strstr(app.headline->label(),"NM")!=nullptr,"Altair IE changes reduce without losing the result");
        }
    }
    for(const char *bad:{"","-1","nan","inf","1x","121"}){
        app.ie->value(bad);app.reduce_button->do_callback();Fl::check();
        require(app.report_buffer.length()==0,"invalid IE clears stale Altair reduction");
    }
    }
    std::puts("PASS Altair/Arcturus 22:06:15: repeated IE on/off changes, sea/artificial/short horizon and invalid-input recovery");
    app.new_sight();
    app.tabs->value(app.almanac_tab);
    for(auto format:{navbook::Format::Pdf,navbook::Format::Latex}) {
        navbook::Options options;
        options.format=format;
        options.sun_moon=options.planets=options.stars=options.events=options.increments=options.corrections=false;
        options.reduction=navbook::Reduction::Ageton;
        auto target=(testdir / (format==navbook::Format::Pdf?"annual-table.pdf":"annual-table.tex")).u8string();
        app.begin_book(options,target);
        require(!app.tabs->active() && !app.engine->active(),"book reserves engine controls");
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(app.book_running && std::chrono::steady_clock::now()<deadline) Fl::wait(.02);
        require(!app.book_running && app.book_error.empty(),"book worker finishes without error");
        require(fs::file_size(fs::u8path(target))>100000,"annual export saved");
        std::ifstream exported(fs::u8path(target),std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(exported)),{});
        require(format==navbook::Format::Pdf?content.find("%PDF-1.4")==0:
                    content.find("\\documentclass[10pt]{article}")!=std::string::npos,
                "worker writes the selected export format");
        require(app.tabs->active() && app.engine->active(),"book releases engine controls");
        std::printf("PASS annual book %s: progress worker, reserved engine and restored controls\n",
                    format==navbook::Format::Pdf?"PDF":"LaTeX");
    }
    require(std::string(app.lat->value()).empty() && std::string(app.lon->value()).empty(),
            "new form without DR defaults has explicitly empty coordinates");
    app.sky_date->value("2026-06-21");app.sky_time->value("00:00:00");
    app.sky_lat->value("34 02.5");app.sky_lon->value("117 21.9");
    app.sky_lon_hemi->value(1);app.update_sky(0);
    require(app.sky_canvas->ready && app.sky_canvas->sky.count==58 &&
            std::abs(app.sky_canvas->sky.sun_altitude+8)<.001,"native evening sky");
    int planned_star=-1;
    for(size_t i=0;i<app.sky_canvas->sky.count;i++)
        if(app.sky_canvas->sky.stars[i].selected){planned_star=int(i);break;}
    require(planned_star>=0,"native suggested star exists");
    app.select_sky_star(planned_star);
    std::string planned_date=app.sky_date->value(), planned_time=app.sky_time->value();
    app.sky_date->value("2000-01-01");app.sky_lat->value("0");
    app.sky_to_sight();
    require(app.tabs->value()==app.sight_tab && app.star->value()==planned_star &&
            std::string(app.date->value())==planned_date && std::string(app.time->value())==planned_time &&
            std::abs(angle(app.lat->value(),"Latitude",90)-(34+2.5/60))<1e-8 &&
            std::string(app.hs->value()).empty() && app.store.n==0,
            "native transfer uses calculated UTC/DR, clears Hs and never saves");
    app.sky_min->value("90");rejects([&]{app.update_sky();});
    require(!app.sky_canvas->ready && !app.sky_sight->active() && app.sky_list->size()==0,
            "invalid native sky clears stale chart, list and transfer");
    app.sky_min->value("15");app.sky_date->value(planned_date.c_str());
    app.update_sky();
    app.sky_time->value("20:00:00");app.update_sky();
    require(app.sky_canvas->sky.bodies[NAV_SUN].altitude>0, "daytime native Sun above horizon");
    require(app.sky_canvas->sky.body_count==7, "Sun, Moon and five planets in native sky");
    for(size_t i=0;i<app.sky_canvas->sky.body_count;i++){
        int index=int(app.sky_canvas->sky.count+i);
        auto object=app.sky_canvas->object(index);
        app.select_sky_star(index);
        require(app.sky_canvas->selected==index, "native solar-system selection");
        if(object.altitude<=0){require(!app.sky_sight->active(), "below-horizon body transfer disabled");continue;}
        app.sky_to_sight();
        require(app.body->value()==int(object.body) && std::string(app.hs->value()).empty() &&
                app.store.n==0 && std::string(app.date->value())==planned_date,
                "native Sun/Moon/planet handoff uses body, planned UTC and empty Hs");
    }
    app.tabs->value(app.sky_tab);
    auto *original_sky=app.sky_canvas;
    app.select_sky_star(int(app.sky_canvas->sky.count+NAV_SUN));
    app.sky_canvas->zoom_by(100);
    require(app.sky_canvas->zoom()==4.5,"sky zoom caps at about five percent of plotted sky area");
    app.sky_canvas->centre_selected();
    double px,py;app.sky_canvas->point(app.sky_canvas->object(app.sky_canvas->selected),px,py);
    require(std::abs(px-app.sky_canvas->x()-app.sky_canvas->w()/2.)<1e-7 &&
            std::abs(py-app.sky_canvas->y()-app.sky_canvas->h()/2.)<1e-7 &&
            app.sky_canvas->object_at(px,py)==app.sky_canvas->selected,"zoomed object centres and remains selectable");
    app.sky_canvas->pan_by(30,-20);
    double moved_x,moved_y;app.sky_canvas->point(app.sky_canvas->object(app.sky_canvas->selected),moved_x,moved_y);
    require(std::abs(moved_x-px-30)<1e-6 && std::abs(moved_y-py+20)<1e-6,"sky panning follows drag pixels");
    app.detach_sky();Fl::check();
    require(app.sky_canvas->zoom()==4.5,"detaching preserves zoom");
    require(app.sky_window->shown() && app.sky_canvas->parent()==app.sky_window.get() &&
            app.sky_placeholder->visible(),"detached sky uses original live canvas");
    app.sky_window->size(1200,900);Fl::check();
    require(app.sky_canvas->w()==1200 && app.sky_canvas->h()==788,"detached sky resizes");
    app.sky_detached_layers[1]->value(1);app.sky_detached_layers[1]->do_callback();
    require(app.sky_canvas->constellation_names && app.sky_layers[1]->value(),"detached names synchronize");
    app.sky_layers[0]->value(0);app.sky_layers[0]->do_callback();
    require(!app.sky_canvas->lines && !app.sky_detached_layers[0]->value(),"docked options synchronize");
    app.toggle_sky_fullscreen();Fl::check();
    require(app.sky_window->fullscreen_active(),"native full screen enters");
    app.toggle_sky_fullscreen();Fl::check();
    require(!app.sky_window->fullscreen_active(),"native full screen exits");
    app.sky_window->do_callback();Fl::check();
    require(!app.sky_window->shown() && app.sky_canvas==original_sky && app.sky_canvas->parent()==app.sky_tab &&
            app.sky_canvas->w()==app.sky_placeholder->w(),"reattach restores live chart and dock size");
    require(app.sky_canvas->zoom()==4.5,"reattaching preserves zoom");
    app.sky_canvas->reset_view();require(app.sky_canvas->zoom()==1,"All sky resets view");
    app.detach_sky();app.sky_min->value("90");rejects([&]{app.update_sky();});
    require(!app.sky_canvas->ready && std::string(app.sky_detached_detail->label()).find("unavailable")!=std::string::npos,
            "detached error clears original live chart");
    app.reattach_sky();app.sky_min->value("15");app.update_sky();
    require(app.sky_canvas->object_count()==65 && app.sky_canvas->sky.context_count==712,
            "supporting stars are separate from selectable navigation objects");
    app.sky_layers[0]->value(1);app.sky_layers[1]->value(0);app.update_sky_layers();
    std::puts("PASS planetarium: native layers, detach/resize/fullscreen/reattach, stars, bodies, sight handoff and errors");
    app.example();
    auto before_missing = app.store.n;
    app.lat->value("");
    app.lon->value("");
    app.reduce_button->do_callback();
    require(std::string(app.status->label()).find("DR latitude is required") != std::string::npos &&
                app.report_buffer.length() == 0,
            "blank DR callback reports a friendly error and clears stale results");
    app.save_button->do_callback();
    require(app.store.n == before_missing, "invalid DR does not save a sight");
    app.lat->value("00 00.0");
    app.lon->value("000 00.0");
    app.lon_hemi->value(0);
    auto zero = app.read_sight();
    require(zero.dr_lat == 0 && zero.dr_lon == 0, "explicit zero/zero remains a valid DR input");
    app.reduce_button->do_callback();
    require(std::string(app.status->label()).find("below the horizon") != std::string::npos &&
                app.report_buffer.length() == 0,
            "unsuitable zero DR is handled without an uncaught callback exception");
    app.example();
    std::puts("PASS native input callbacks: blank DR, no invalid save, explicit zero/zero and "
              "below-horizon rejection");
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
    app.at_dr->value(1);
    auto direct_sight = app.read_sight();
    auto direct_result = reduce(direct_sight);
    require(direct_sight.reduce_at_dr == 1 && direct_result.ap_lat == direct_sight.dr_lat &&
                direct_result.ap_lon == direct_sight.dr_lon, "checkbox reduces at the full DR");
    app.reduce_button->do_callback();
    char *direct_report = app.report_buffer.text();
    bool have_direct_report = std::string(direct_report).find("Direct reduction at DR") != std::string::npos;
    std::free(direct_report);
    require(have_direct_report, "direct method appears in native worked report");
    app.save_button->do_callback();
    require(app.store.n == 1 && app.store.v[0].reduce_at_dr == 1, "Save as new preserves method");
    app.load_store();
    app.fill_sight(app.store.v[0]);
    require(app.at_dr->value() == 1, "reopening native sight restores checkbox");
    app.at_dr->value(0);
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
    require(defaults.short_horizon_nm==0,"existing defaults keep normal dip");
    app.fill_defaults();app.def_horizon->value(0);app.def_lat->value("0");app.def_lon->value("0");
    app.def_at_dr->value(1);app.def_temp->value("0");app.def_press->value("1015");
    app.def_height->value("10");app.def_ie->value("1.2");app.def_arc->value(1);
    app.def_motion->value(1);app.def_course->value("270");app.def_speed->value("5");
    app.def_set->value("90");app.def_drift->value("1.5");app.save_setup_defaults();
    {
        Workbench restored;
        require(std::string(restored.lat->value())=="00 00.0" &&
                std::string(restored.al_lat->value())=="00 00.0" &&
                std::string(restored.sky_lat->value())=="00 00.0","zero DR restores into all three pages at startup");
        require(restored.at_dr->value() && restored.arc->value() && restored.motion->value() &&
                number(restored.temp->value(),"temperature",-100,100)==0 &&
                number(restored.drift->value(),"drift",0,100)==1.5 &&
                std::string(restored.hs->value()).empty(),"settings restore without a stale measured Hs");
    }
    // Copy/load do not need Hs and do not change page UTC or another page's edits.
    app.lat->value("12 30");app.lon->value("45 15");app.lat_hemi->value(1);app.lon_hemi->value(1);
    app.hs->value("");app.at_dr->value(1);app.save_defaults();
    std::string old_sky_time=app.sky_time->value();
    app.copy_sight_position(app.al_lat,app.al_lat_hemi,app.al_lon,app.al_lon_hemi);
    app.load_saved_position(app.sky_lat,app.sky_lat_hemi,app.sky_lon,app.sky_lon_hemi);
    require(app.al_lat_hemi->value()==1 && app.al_lon_hemi->value()==1 &&
            app.sky_lat_hemi->value()==1 && app.sky_lon_hemi->value()==1 &&
            std::string(app.sky_time->value())==old_sky_time,"saved and current DR copy hemisphere and retain UTC without Hs");
    app.al_date->value("2026-09-19");app.al_time->value("21:38:05.125");
    for(int b:{0,1,4,8}){
        app.al_body->value(b);app.al_star->value(nav_star_find("Vega"));app.almanac_to_sight();
        require(app.body->value()==(b==8?7:b) && std::string(app.time->value())=="21:38:05.125" &&
                app.lat_hemi->value()==1 && app.lon_hemi->value()==1 && app.at_dr->value() &&
                std::string(app.hs->value()).empty() && app.edit_id==0,"almanac handoff preserves exact UTC/DR/method and leaves Hs blank");
    }
    app.al_body->value(7);rejects([&]{app.almanac_to_sight();});
    app.al_body->value(0);app.al_lat->value("");rejects([&]{app.almanac_to_sight();});
    app.example();app.at_dr->value(0);app.motion->value(0);app.save_defaults();
    std::puts("PASS defaults page: save/reload, explicit zero DR, exact method, current, blank Hs, hemispheres and almanac handoff");
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
    require(report.find("Local Mean Time (LMT), 2026-09-19")!=std::string::npos &&
            report.find("Meridian passage")!=std::string::npos &&
            report.find("west subtracts")!=std::string::npos,
            "native almanac includes daily rise, meridian and set in LMT below corrections");
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
        auto valid_sights = std::vector<Sight>(app.store.v, app.store.v + app.store.n);
        for (size_t i = 0; i < app.store.n; ++i)
            app.store.v[i].dr_lat = app.store.v[i].dr_lon = 0;
        app.fix_button->do_callback();
        require(!app.plot->ready && app.fix_buffer.length() == 0 &&
                    std::string(app.status->label()).find("Saved sight #") != std::string::npos,
                "Calculate selected fix rejects this unsuitable zero-DR fixture without drawing a "
                "result");
        std::copy(valid_sights.begin(), valid_sights.end(), app.store.v);
        app.store.v[1] = app.store.v[0];
        for (int i = 1; i <= app.saved->size(); ++i)
            app.saved->select(i, i <= 2);
        app.fix_button->do_callback();
        require(!app.plot->ready &&
                    std::string(app.status->label()).find("parallel") != std::string::npos,
                "Calculate selected fix handles no solution politely");
        std::copy(valid_sights.begin(), valid_sights.end(), app.store.v);
        for (int i = 1; i <= app.saved->size(); ++i)
            app.saved->select(i);
        app.fix_button->do_callback();
        require(app.plot->ready, "fix succeeds again after correcting inputs");
        std::puts("PASS Calculate selected fix callbacks: unsuitable zero/zero, parallel/no "
                  "solution, recovery");
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
    {
        // Airless star altitudes at known positions, with an imposed raw Hs bias.
        // Mix sea and doubled AH readings, and include a deliberately bad shot.
        const std::string log_before = [&] {
            std::ifstream f(log, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(f), {});
        }();
        navsight_store_free(&app.store);
        const double rad = std::acos(-1.)/180;
        for (int i = 0; i < 11; ++i) {
            Sight v{};
            date_time("2026-09-20", "03:15:30", v);
            v.mi += i;
            std::strcpy(v.body, "star");
            std::strcpy(v.star, "Alpheratz");
            v.dr_lat = 34.04 + i*.003;
            v.dr_lon = -117.36 + i*.002;
            v.artificial = i%2;
            v.ie_on_arc = 1;
            v.ie_min = 1.2;
            NavStarAlmanac star{};
            require(!nav_star_almanac(nav_star_find(v.star), navsight_jd(&v), &star),
                    "reference star position");
            const double lat = v.dr_lat*rad, dec = star.dec_deg*rad;
            const double hc = std::asin(std::sin(lat)*std::sin(dec) +
                std::cos(lat)*std::cos(dec)*std::cos((star.gha_deg+v.dr_lon)*rad))/rad;
            v.hs_deg = hc*(v.artificial ? 2 : 1) + (i==10 ? 10. : 2.4)/60;
            NavIndexCheck c{};
            char err[512]{};
            require(!navsight_index_check(&v, &c, err, sizeof err), err);
            require(std::fabs(c.required_ic_min + (i==10 ? 10. : 2.4)) < .001,
                    "recovers the raw index error without table rounding");
            if (i < 10) {
                require(std::fabs(c.residual_saved_min - (v.artificial ? .6 : 1.2)) < .001 &&
                        std::fabs(c.residual_zero_min - (v.artificial ? 1.2 : 2.4)) < .001,
                        "applied versus zero correction, with IC before AH halving");
            }
            navsight_store_add(&app.store, &v);
            require(app.store.n == size_t(i+1), "add index check fixture");
        }
        app.refresh();
        app.saved->select(1);
        app.index_button->do_callback();
        require(std::string(app.status->label()).find("at least three") != std::string::npos,
                "too few selected sights rejected politely");
        for (int i = 1; i <= 10; ++i) app.saved->select(i);
        app.index_button->do_callback();
        auto index_report = [&] {
            char *p = app.index_buffer.text();
            std::string report(p);
            std::free(p);
            return report;
        };
        std::string report = index_report();
        require(report.find("10 selected sights") != std::string::npos &&
                report.find("2.40' on the arc") != std::string::npos &&
                report.find("+0.90'") != std::string::npos &&
                report.find("+1.80'") != std::string::npos,
                "selected ten sights, expected sign and both mean residuals");
        app.saved->select(11);
        app.index_button->do_callback();
        require(index_report().find("11 selected sights") != std::string::npos &&
                index_report().find("2.40' on the arc") != std::string::npos &&
                index_report().find("-10.00'") != std::string::npos,
                "outlier is visible and does not shift the median");
        for (size_t i = 0; i < 10; ++i) {
            app.store.v[i].hs_deg -= 4.1/60;
            app.store.v[i].ie_min = i%2 ? 1.2 : 0;
            app.store.v[i].ie_on_arc = 0;
        }
        app.saved->select(11,0);
        app.index_button->do_callback();
        require(index_report().find("1.70' off the arc") != std::string::npos,
                "off-arc estimate with mixed saved index corrections");
        std::ifstream f(log, std::ios::binary);
        require(std::string(std::istreambuf_iterator<char>(f), {}) == log_before &&
                app.plot->ready && app.expanded_plot->ready,
                "index check leaves the saved log and current plots alone");
        std::puts("PASS index check: selected sights, on/off arc, saved vs zero IC, artificial horizon, outlier and unchanged log/plots");
    }
    {
        Workbench::StoreGuard input;
        char err[512]{};
        const double expected[]={.8,1.0,1.2,16.0,1.1,.9};
        for(int i=0;i<6;i++){
            Sight v{};
            std::strcpy(v.body,"venus");
            date_time("2026-11-09","13:15:00",v);
            v.mi+=i; v.dr_lat=34.02; v.dr_lon=-117.31; v.reduce_at_dr=i%2;
            v.artificial=i%2; v.he=v.artificial?0:8; v.ie_min=5; v.ie_on_arc=1;
            v.temp_c=10; v.press_mb=1017; v.hs_deg=v.artificial?20:10;
            auto r=reduce(v);
            const double d=r.dec_exact*M_PI/180,p=v.dr_lat*M_PI/180,h=(r.gha_exact+v.dr_lon)*M_PI/180;
            const double target=std::asin(std::sin(d)*std::sin(p)+std::cos(d)*std::cos(p)*std::cos(h))*180/M_PI+expected[i]/60;
            for(int j=0;j<5;j++)v.hs_deg+=(v.artificial?2:1)*(target-reduce(v).ho);
            require(navsight_store_add(&input.data,&v)>0,"add cluster test sight");
        }
        require(!navsight_store_save(&input.data,err,sizeof err),err);
        app.load_store();
        for(int i=1;i<=6;i++)app.saved->select(i);
        app.cluster_preview_button->do_callback();
        require(app.showing_cluster && app.cluster_plot->visible() && !app.plot->visible() &&
                app.cluster.sight.cluster_used==5 && !app.cluster.keep[3] &&
                std::fabs(app.cluster.offset_min-1)<.001 && app.cluster.sight.mi==17,
                "cluster preview normalizes rising Venus and rejects only the injected outlier");
        require(app.cluster.hc_deg[5]-app.cluster.hc_deg[0]>.9 &&
                std::fabs(reduce(app.cluster.sight).intercept_nm-1)<.11 &&
                app.cluster_plot->points.size()==6 && !app.cluster_plot->points[3].kept,
                "the graph shows all observed altitudes and representative retains the intercept");
        app.cluster_reject->value(0); app.cluster_reject->do_callback();
        require(app.cluster.sight.cluster_used==6 && app.cluster.offset_min>3,
                "outlier toggle recomputes the fit and includes all points");
        app.cluster_reject->value(1); app.cluster_reject->do_callback();
        app.cluster_save_button->do_callback();
        require(app.store.n==7 && app.store.v[6].cluster_used==5 && !app.showing_cluster && app.plot->visible(),
                "keep originals writes one independent LOP and restores chart");
        auto good=(testdir/"sights backup.save").u8string();
        auto bad=(testdir/"truncated backup.save").u8string();
        app.export_sight_log(good);
        std::ofstream(bad)<<"# navalm sight backup 1 count=3\n";
        rejects([&]{app.import_sight_log(bad);});
        require(app.store.n==7,"failed restore leaves the live sight log intact");
        app.saved->deselect();
        for(int i=1;i<=6;i++)app.saved->select(i);
        app.cluster_preview_button->do_callback();
        app.saved->select(1,0);
        rejects([&]{app.save_cluster(true);});
        app.saved->select(1);
        app.cluster_replace_button->do_callback();
        require(app.store.n==2 && app.store.v[0].id==7 && app.store.v[1].id==8 &&
                app.store.v[1].cluster_used==5,
                "replace atomically removes the full selection and leaves unrelated LOPs");
        app.import_sight_log(good);
        require(app.store.n==7 && app.store.v[0].id==1 && app.store.v[6].id==7 &&
                app.store.v[6].cluster_used==5 && app.store.v[6].cluster_total==6 &&
                std::fabs(reduce(app.store.v[6]).intercept_nm-1)<.11,
                "backup round trip preserves raw rows, IDs and corrected representative LOP");
        NavIndexCheck diagnostic{};
        require(navsight_index_check(&app.store.v[6],&diagnostic,err,sizeof err)!=0,
                "representative LOP has no raw Hs for index checks");
        NavSightCluster invalid{};
        input.data.v[1].dr_lon+=.1;
        require(navsight_cluster(input.data.v,2,1,&invalid,err,sizeof err)!=0 &&
                std::strstr(err,"same saved DR"),"different DR rejected");
        input.data.v[1].dr_lon=input.data.v[0].dr_lon;
        std::strcpy(input.data.v[1].body,"sun");
        require(navsight_cluster(input.data.v,2,1,&invalid,err,sizeof err)!=0 &&
                std::strstr(err,"same body"),"mixed bodies rejected");
        std::puts("PASS cluster: moving Venus, per-shot corrections, graph/outlier toggle, keep/replace, stale selection, backup/restore");
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
    {
        auto next=app.clock_log;int id=next.add_clock("Test watch");app.chrono_commit(next);
        app.chrono_ref_date->value("2026-10-30");app.chrono_ref_time->value("12:00:00");
        app.chrono_watch_date->value("2026-10-30");app.chrono_watch_time->value("12:00:10");
        app.chrono_source->value(0);app.chrono_add_button->do_callback();
        app.chrono_ref_date->value("2026-11-01");app.chrono_watch_date->value("2026-11-01");
        app.chrono_watch_time->value("12:00:14");app.chrono_add_button->do_callback();
        require(app.clock_log.readings.size()==2 && std::abs(app.clock_log.model(id).rate-2)<1e-9,"Chrono comparison callbacks fit signed drift");
        app.chrono_add_button->do_callback();
        require(app.clock_log.readings.size()==2 && std::string(app.status->label()).find("already logged")!=std::string::npos,"duplicate clock comparison is rejected politely");
        app.chrono_load();require(app.clock_log.readings.size()==2,"native Chrono survives reload");
        app.example();app.sight_watch_date->value("2026-11-01");app.sight_watch_time->value("22:06:15");
        auto direct_utc=app.read_sight();require(direct_utc.y==2026 && direct_utc.mo==9 && !direct_utc.clock_name[0],"unchecked watch fields do not replace entered UTC");
        app.sight_clock_apply->value(1);auto corrected=app.read_sight();
        require(corrected.clock_correction_sec< -14 && corrected.clock_correction_sec> -16,"fast watch receives a negative sight-time correction");
        auto original=chrono_nav::parse("2026-11-01","22:06:15");
        require(std::abs(chrono_nav::epoch(corrected)-app.clock_log.model(id).utc_from_watch(original))<.001,"native sight uses model inversion before reduction");
        app.show_reduction(corrected,reduce(corrected));
        std::unique_ptr<char,decltype(&std::free)> chrono_report(app.report_buffer.text(),std::free);
        require(std::string(app.time->value())==chrono_nav::stamp(chrono_nav::epoch(corrected)).substr(11) &&
                std::string(chrono_report.get()).find("Watch time  2026-11-01T22:06:15.000")!=std::string::npos,"reduction fills UTC and reports the separate original watch time");
        app.fill_sight(corrected);auto reopened=app.read_sight();
        require(!app.sight_clock_apply->value() && std::abs(navsight_jd(&reopened)-navsight_jd(&corrected))<1e-10 && reopened.clock_name[0],"reopening corrected sight preserves UTC and audit without double correction");
        auto changed=app.clock_log;changed.readings.back().watch+=2;app.chrono_commit(changed);
        reopened=app.read_sight();require(std::abs(navsight_jd(&reopened)-navsight_jd(&corrected))<1e-10,"later clock-model changes do not rewrite a saved correction");
        app.clock_log.apply_new=true;app.new_sight();require(app.sight_clock_apply->value() && !*app.sight_watch_time->value(),"new manual watch sight requires an explicit watch reading");
        app.al_body->value(0);app.al_date->value("2026-11-01");app.al_time->value("22:06:15");app.al_lat->value("34 02.5");app.al_lon->value("117 21.9");app.al_lon_hemi->value(1);app.almanac_to_sight();
        require(!app.sight_clock_apply->value() && std::string(app.time->value())=="22:06:15","almanac UTC handoff bypasses watch correction");
        app.chrono_computer_now();require(app.chrono_source->value()==2 && std::string(app.chrono_ref_time->value()).find('.')!=std::string::npos,"computer reference captures fractional UTC and source");
        std::puts("PASS native Chrono: comparison callbacks, duplicate guard, reload, drift correction, frozen saved UTC, handoff bypass and computer UTC capture");
    }
    app.engine->value(1);
    app.change_engine();
    require(!app.plot->ready && app.fix_buffer.length() == 0 && !app.sky_canvas->ready &&
            !app.sky_sight->active(), "engine change clears stale fix and sky");
    app.win.hide();
    Fl::check();
    std::puts("All desktop checks passed; no personal navigation files used.");
    return 0;
}
