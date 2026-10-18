/* SPDX-License-Identifier: MIT
 * Native FLTK workbench over NAVALM's unchanged C calculation engine.
 */
#include "nav_version.h"
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
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
namespace fs = std::filesystem;
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
    bool ready = false, visible[4] = {true, true, true, true};
    double opacity = 1;
    Plot(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
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
        int side = std::min(w() - 36, h() - 36), left = x() + (w() - side) / 2,
            top = y() + (h() - side) / 2;
        fl_push_clip(left, top, side, side);
        auto xy = [&](GeoPoint g) {
            auto p = scene.plane(g);
            return std::make_pair(left + side * .5 * (1 + p.first / scene.extent),
                                  top + side * .5 * (1 - p.second / scene.extent));
        };
        auto color = [&](int n) {
            const char *s = vector_color(n);
            unsigned r, g, b;
            std::sscanf(s, "#%02x%02x%02x", &r, &g, &b);
            fl_color(fl_color_average(fl_rgb_color(r, g, b), paper, (float)opacity));
        };
        for (auto &p : scene.paths)
            if (visible[p.layer]) {
                color(p.color);
                fl_line_style(p.dashed ? FL_DASH : FL_SOLID, p.layer == 0 ? 1 : 2);
                for (size_t i = 1; i < p.points.size(); ++i) {
                    auto a = xy(p.points[i - 1]), b = xy(p.points[i]);
                    fl_line((int)a.first, (int)a.second, (int)b.first, (int)b.second);
                }
            }
        fl_line_style(FL_SOLID);
        fl_font(FL_HELVETICA, 11);
        for (auto &l : scene.labels)
            if (visible[l.layer]) {
                color(l.color);
                auto a = xy(l.point);
                fl_draw(l.text.c_str(), (int)a.first + 4, (int)a.second - 5);
            }
        fl_pop_clip();
        fl_color(fl_rgb_color(160, 179, 184));
        fl_rect(left, top, side, side);
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
    Fl_Check_Button *motion, *fix_at, *layers[4];
    Fl_Value_Slider *opacity;
    Fl_Multi_Browser *saved;
    Plot *plot;
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
        Fl::set_font(FL_HELVETICA, "Helvetica");
        Fl::set_font(FL_COURIER, "Menlo");
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
        al_lat = input(36, 232, 190, "DR latitude · deg min");
        al_lat_hemi = choice(236, 232, 68, "", "N|S");
        al_lon = input(324, 232, 190, "DR longitude · deg min");
        al_lon_hemi = choice(524, 232, 68, "", "E|W");
        button(616, 232, 160, "Calculate almanac", [this] { almanac(false); }, true);
        button(792, 232, 160, "Hourly table", [this] { almanac(true); });
        button(968, 232, 173, "Export text…", [this] { export_text(al_buffer, "almanac.txt"); });
        al_display = display(36, 300, 1105, 475, al_buffer);
        almanac_tab->resizable(al_display);
        almanac_tab->end();
        sight_tab = new Fl_Group(16, 130, 1148, 672, "Sight reduction");
        body = choice(36, 168, 210, "Observed body",
                      "Sun|Moon|Mercury|Venus|Mars|Jupiter|Saturn|Navigation star");
        star = choice(266, 168, 210, "Navigation star", "");
        fill_stars(star);
        date = input(36, 230, 210, "UTC date · YYYY-MM-DD");
        time = input(266, 230, 210, "UTC time · HH:MM:SS");
        hs = input(36, 292, 210, "Sextant Hs · deg min");
        label = input(266, 292, 210, "Sight label");
        label->maximum_size(NAVSIGHT_LABEL - 1);
        lat = input(36, 354, 132, "DR latitude · deg min");
        lat_hemi = choice(178, 354, 68, "", "N|S");
        lon = input(266, 354, 132, "DR longitude · deg min");
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
        auto *input_hint = new Fl_Box(
            703, 738, 434, 45,
            "Angles accept decimal degrees or degrees and minutes.\nAll dates and times are UTC.");
        input_hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        input_hint->labelsize(12);
        sight_tab->resizable(report_display);
        sight_tab->end();
        fix_tab = new Fl_Group(16, 130, 1148, 672, "Saved sights + plotting");
        auto *hint = new Fl_Box(36, 140, 430, 40,
                                "Select 2–16 sights for a stationary or running fix.\nUse Command "
                                "/ Shift to select multiple rows.");
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
        const char *names[] = {"Grid / compass", "LOPs / motion", "Labels", "Fix / ellipse"};
        for (int i = 0; i < 4; ++i) {
            layers[i] = new Fl_Check_Button(493 + i * 163, 143, 163, 28, names[i]);
            layers[i]->labelsize(12);
            layers[i]->value(1);
            bind(layers[i], [this] { update_layers(); });
        }
        plot = new Plot(493, 183, 645, 512);
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
        auto *plot_hint = new Fl_Box(
            493, 749, 645, 31,
            "Local tangent plane · equal NM scale · vector overlay · no chart underlay yet");
        plot_hint->labelsize(12);
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
        chart_display = display(36, 309, 1105, 466, chart_buffer);
        auto *charts = chart_display;
        charts->textsize(15);
        chart_buffer.text(
            "CHARTS + UNIVERSAL PLOTTING CHART\n\nThe plotting sheet is an independent geographic "
            "vector overlay.\nIt carries graticule, compass circle, assumed positions, "
            "intercepts,\nlines of position, motion advances, labels, fix and uncertainty "
            "ellipse.\n\nLayer visibility and opacity are available in Saved sights + "
            "plotting.\nExport a transparent SVG for artwork, or GeoJSON paths for a chart "
            "renderer.\nSVG uses a local tangent plane; its metadata records the origin and "
            "scale.\nA chart renderer must reproject the geographic vectors to its own "
            "projection.\n\nNEXT: chart catalogue → download manager → local chart database → "
            "chart canvas.\nThe chart canvas can draw the plotting vectors above its chart "
            "layer.\nDownloading, chart rendering and georeferenced chart compositing are "
            "not\nimplemented in this version. The existing CLI and browser remain "
            "available.\n\nDESKTOP STATE\nThis sandbox keeps saved observations and defaults in "
            "its .local directory.\nNAVALM_SAVE and NAVALMRC override those paths. Save as new "
            "adds a sight;\nUpdate edited replaces the selected ID. Calculations use the same C "
            "engine\nas the command-line program. Changes to engine or log clear the old "
            "fix.\n\nUse this experimental workbench alongside independently checked navigation.");
        chart_tab->resizable(charts);
        chart_tab->end();
        tabs->end();
        status = new Fl_Box(22, 811, 1136, 28, "Ready");
        status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        status->labelsize(12);
        win.resizable(tabs);
        win.size_range(1180, 850);
        win.end();
        win.callback([](Fl_Widget *w, void *) { w->hide(); });
        new_sight();
        now(al_date, al_time);
        al_lat->value("0");
        al_lon->value("0");
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
    }
    ~Workbench() {
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
        hs->value(fmt(s.hs_deg, 9).c_str());
        label->value(s.label);
        lat->value(fmt(std::fabs(s.dr_lat), 9).c_str());
        lon->value(fmt(std::fabs(s.dr_lon), 9).c_str());
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
        plot->redraw();
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
    void update_layers() {
        for (int i = 0; i < 4; ++i)
            plot->visible[i] = layers[i]->value();
        plot->opacity = opacity->value();
        plot->redraw();
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
        write_file(path,
                   geo ? geojson(plot->scene) : svg(plot->scene, plot->visible, plot->opacity));
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
    fs::path executable = fs::absolute(arg);
#ifdef __APPLE__
    char path[4096];
    uint32_t n = sizeof path;
    if (_NSGetExecutablePath(path, &n) == 0)
        executable = fs::weakly_canonical(path);
#endif
    auto root = executable.parent_path();
    if (root.filename() == "MacOS" && root.parent_path().filename() == "Contents")
        return root.parent_path().parent_path().parent_path();
    return root;
}
static int self_test();
int main(int argc, char **argv) {
    bool checks = std::string(argv[0]).find("navalm-checks") != std::string::npos;
    try {
        auto root = runtime_root(argv[0]);
        fs::create_directories(root / ".local");
        if (checks) {
            fs::current_path(root);
            fs::create_directories(root / ".build/desktop-test");
            std::freopen((root / ".build/desktop-test/ui-checks.txt").c_str(), "w", stdout);
            std::freopen((root / ".build/desktop-test/ui-errors.txt").c_str(), "w", stderr);
            setvbuf(stdout, nullptr, _IONBF, 0);
        }
        if (!std::getenv("NAVALM_SAVE"))
            setenv("NAVALM_SAVE", (root / ".local/desktop-sights.save").c_str(), 0);
        if (!std::getenv("NAVALMRC"))
            setenv("NAVALMRC", (root / ".local/desktop-defaults.rc").c_str(), 0);
        if (!std::getenv("NAVALM_EPHEMERIS")) {
            auto ep = root / "ephem/de440s.nav";
            auto bundle = fs::path(argv[0]).parent_path() / "../Resources/de440s.nav";
            if (fs::exists(ep))
                nav_engine_set_ephemeris_path(ep.c_str());
            else if (fs::exists(bundle))
                nav_engine_set_ephemeris_path(bundle.c_str());
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
            int code = self_test();
            nav_engine_shutdown();
            if (checks) {
                Fl_Double_Window result(740, 260, "NAVALM desktop checks passed");
                Fl_Box text(24, 24, 692, 205,
                            "All desktop checks passed.\n\nSun reduction · save / edit / reload · "
                            "star and hourly almanacs\nStationary and running fixes · vector "
                            "geometry · layer controls\nTransparent SVG / geographic exports · "
                            "engine switching\n\nReport: .build/desktop-test/ui-checks.txt");
                text.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
                text.labelsize(15);
                result.end();
                result.show();
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
    char pattern[] = "/tmp/navalm-desktop-test-XXXXXX";
    char *dir = mkdtemp(pattern);
    if (!dir)
        throw std::runtime_error("Cannot create test directory.");
    fs::path testdir = dir;
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(path, e);
        }
    } cleanup{testdir};
    auto log = (testdir / "sights.save").string(), rc = (testdir / "defaults.rc").string();
    setenv("NAVALM_SAVE", log.c_str(), 1);
    setenv("NAVALMRC", rc.c_str(), 1);
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
    auto s = app.read_sight();
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
              "star/hourly almanac");
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
        require(svgtext.find("<rect width=\"1000\" height=\"1000\"/>") != std::string::npos,
                "clip rectangle");
        require(svgtext.find("<rect fill=") == std::string::npos, "SVG has no painted background");
        auto vectors = geojson(o);
        require(vectors.find("FeatureCollection") != std::string::npos,
                "GeoJSON geographic vectors");
        write_file((output / (std::string(name) + ".svg")).string(), svgtext);
        write_file((output / (std::string(name) + ".geojson")).string(), vectors);
        app.layers[0]->value(0);
        app.opacity->value(.55);
        app.update_layers();
        require(!app.plot->visible[0] && app.plot->opacity == .55, "layer controls");
        app.layers[0]->value(1);
        app.fix_at->value(1);
        app.fix_date->value("2026-09-20");
        app.fix_time->value("04:00:00");
        app.calculate_fix();
        require(app.fix_result.fh == 4, "explicit fix UTC");
        app.fix_at->value(0);
        std::printf("PASS %s fix: %.8f %.8f; vector residuals, geographic anchors, SVG/GeoJSON, "
                    "layers, explicit UTC\n",
                    name, f.lat, f.lon);
    }
    app.engine->value(1);
    app.change_engine();
    require(!app.plot->ready && app.fix_buffer.length() == 0, "engine change clears stale fix");
    app.win.hide();
    Fl::check();
    std::puts("All desktop checks passed; no personal navigation files used.");
    return 0;
}
