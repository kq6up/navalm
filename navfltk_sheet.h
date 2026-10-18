/* SPDX-License-Identifier: MIT
 * Native vector port of page.html's plotSheet(). Its 760 x 772 drawing rules
 * are retained here; SVG and FLTK consume the same primitives. Main-chart
 * vectors also retain geographic coordinates for a future chart renderer.
 * Include via navfltk_plot.h.
 */
#ifndef NAVFLTK_SHEET_H
#define NAVFLTK_SHEET_H
#include <array>
namespace desktop {
struct Point {
    double x, y;
};
struct Box {
    double x, y, w, h;
};
struct Ink {
    std::string stroke = "none", fill = "none", dash;
    double width = 1, alpha = 1, fill_alpha = 1;
};
struct Mark {
    std::vector<Point> points;
    Ink ink;
    int layer = 0, clip = -1;
    bool closed = false, map_geometry = false;
    std::string role;
};
struct Caption {
    Point point;
    std::string text, color = "#1f2a30", anchor = "start";
    double size = 11;
    int layer = 2, clip = -1;
    bool mono = false, bold = false, italic = false, halo = false, map_geometry = false;
};
struct Sheet {
    static constexpr double width = 760, height = 772;
    std::array<Box, 2> clips{{{46, 46, 668, 496}, {0, 0, 0, 0}}};
    std::vector<Mark> marks;
    std::vector<Caption> captions;
    // Preserve web paint order while retaining independent layer visibility.
    std::vector<std::pair<bool, size_t>> order;
    double ce = 0, cn = 0, scale = 1, pcx = 380, pcy = 294, grid_step = 1, lon_step = 1;
    Point canvas(double e, double n) const {
        return {pcx + (e - ce) * scale, pcy - (n - cn) * scale};
    }
    GeoPoint geographic(const Overlay &o, Point p) const {
        return o.geographic(ce + (p.x - pcx) / scale, cn - (p.y - pcy) / scale);
    }
    void path(std::vector<Point> points, const Ink &ink, int layer, int clip, bool closed = false,
              const char *role = "", bool geographic = true) {
        order.push_back({false, marks.size()});
        marks.push_back({points, ink, layer, clip, closed, geographic && clip == 0, role});
    }
    void line(Point a, Point b, const Ink &ink, int layer, int clip, const char *role = "",
              bool geo = true) {
        path({a, b}, ink, layer, clip, false, role, geo);
    }
    void rect(Box b, const Ink &ink, int layer, int clip, const char *role = "", bool geo = false) {
        path({{b.x, b.y}, {b.x + b.w, b.y}, {b.x + b.w, b.y + b.h}, {b.x, b.y + b.h}}, ink, layer,
             clip, true, role, geo);
    }
    void circle(Point p, double r, const Ink &ink, int layer, int clip, const char *role = "",
                bool geo = true) {
        std::vector<Point> v;
        for (int i = 0; i < 96; ++i) {
            double a = i * 2 * M_PI / 96;
            v.push_back({p.x + r * std::cos(a), p.y + r * std::sin(a)});
        }
        path(v, ink, layer, clip, true, role, geo);
    }
    void text(Point p, const std::string &t, int layer, int clip, double size = 11,
              const std::string &color = "#1f2a30", const std::string &anchor = "start",
              bool mono = false, bool bold = false, bool italic = false, bool halo = false,
              bool geo = false) {
        order.push_back({true, captions.size()});
        captions.push_back({p, t, color, anchor, size, layer, clip, mono, bold, italic, halo, geo});
    }
};
inline double nice_interval(double target) {
    double p = std::pow(10, std::floor(std::log10(target)));
    for (double v : {1., 2., 5., 10.})
        if (v * p >= target)
            return v * p;
    return 10 * p;
}
inline std::string decimal(double v, int n = 1) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(n) << v;
    return s.str();
}
inline const char *lop_color(size_t i) {
    const char *c[] = {"#b4472e", "#2d6a4f", "#3a5a8c", "#8a6d1f", "#6b3a7a", "#2e6f74"};
    return c[i % 6];
}
inline Sheet make_sheet(const Overlay &o) {
    Sheet d;
    auto L = geometry(o.reductions, o.fix);
    const double W = 760, H = 772, PAD = 46, CAP = 34, STRIP = 150, pw = W - 2 * PAD,
                 ph = H - 2 * PAD - CAP - STRIP;
    const std::string INK = "#1f2a30", MUT = "#5a6b74", GRID = "#dde6ec", GRIDJ = "#a8bcc9",
                      ROSE = "#4a5a66", FIXC = "#8b1a1a";
    double emin = 0, emax = 0, nmin = 0, nmax = 0;
    for (auto &l : L)
        for (auto p : {Point{l.original_e, l.original_n}, Point{l.ae, l.an}, Point{l.e, l.n}}) {
            emin = std::min(emin, p.x);
            emax = std::max(emax, p.x);
            nmin = std::min(nmin, p.y);
            nmax = std::max(nmax, p.y);
        }
    d.ce = (emin + emax) / 2;
    d.cn = (nmin + nmax) / 2;
    double span = std::max({emax - emin, nmax - nmin, 1.5}) * 1.6;
    d.scale = std::min(pw, ph) / span;
    d.pcx = PAD + pw / 2;
    d.pcy = PAD + ph / 2;
    auto P = [&](double e, double n) { return d.canvas(e, n); };
    // Sheet backdrop is supplied by the native canvas, not baked into exports.
    d.rect({PAD, PAD, pw, ph}, {ROSE, "none", "", 1.2}, 0, -1, "frame");
    double step = 0;
    for (double c : {.5, 1., 2., 5., 10., 15., 30., 60.})
        if (span / c <= 11) {
            step = c;
            break;
        }
    if (!step)
        step = nice_interval(span / 11);
    double CL = std::cos(o.grid_lat * M_PI / 180), halfN = (ph / 2) / d.scale,
           halfE = (pw / 2) / d.scale;
    double lonStep = std::max(step, nice_interval(2 * halfE / CL / 20));
    d.grid_step = step;
    d.lon_step = lonStep;
    double latC = o.lat * 60 + d.cn, lonC = o.lon * 60 + d.ce / CL;
    auto dm = [&](double v, bool latitude) {
        char h = v < 0 ? (latitude ? 'S' : 'W') : (latitude ? 'N' : 'E');
        double a = std::fabs(v);
        int deg = (int)std::floor(a / 60);
        double m = a - deg * 60;
        return std::string(1, h) + " " + std::to_string(deg) + "° " + (m < 10 ? "0" : "") +
               decimal(m, step < 1 ? 1 : 0) + "'";
    };
    for (int k = (int)std::floor((latC - halfN) / step); k <= (int)std::ceil((latC + halfN) / step);
         ++k) {
        double v = k * step, y = d.pcy - (v - latC) * d.scale;
        if (y < PAD || y > PAD + ph)
            continue;
        d.line({PAD, y}, {PAD + pw, y}, {GRIDJ, "none", "", .8}, 0, 0, "parallel");
        d.text({PAD + 4, y - 4}, dm(v, true), 0, 0, 9, MUT, "start", true);
    }
    for (int k = (int)std::floor((lonC - halfE / CL) / lonStep);
         k <= (int)std::ceil((lonC + halfE / CL) / lonStep); ++k) {
        double v = k * lonStep, x = d.pcx + (v - lonC) * CL * d.scale;
        if (x < PAD || x > PAD + pw)
            continue;
        d.line({x, PAD}, {x, PAD + ph}, {GRIDJ, "none", "", .8}, 0, 0, "meridian");
        if (x < PAD + pw - 62)
            d.text({x + 3, PAD + ph - 5}, dm(v, false), 0, 0, 9, MUT, "start", true);
    }
    double RR = std::min(pw, ph) * .40;
    d.circle({d.pcx, d.pcy}, RR, {ROSE, "none", "", 1, .8}, 0, 0, "compass-rose");
    for (int deg = 0; deg < 360; deg += 5) {
        double a = deg * M_PI / 180, r1 = RR - (deg % 10 == 0 ? 11 : 6);
        d.line({d.pcx + std::sin(a) * r1, d.pcy - std::cos(a) * r1},
               {d.pcx + std::sin(a) * RR, d.pcy - std::cos(a) * RR},
               {ROSE, "none", "", deg % 10 == 0 ? .8 : .4, .75}, 0, 0, "compass-tick");
        if (deg % 30 == 0) {
            char b[8];
            std::snprintf(b, sizeof b, "%03d", deg);
            d.text({d.pcx + std::sin(a) * (RR - 20), d.pcy - std::cos(a) * (RR - 20) + 3}, b, 0, 0,
                   8.5, ROSE, "middle", true);
        }
    }
    double LL = std::max(span * .42, 1.2);
    for (size_t i = 0; i < L.size(); ++i) {
        auto &l = L[i];
        std::string c = lop_color(i);
        double pe = l.un, pn = -l.ue;
        if (o.fix.advanced_nm[i] > 0)
            d.line(P(l.original_e, l.original_n), P(l.ae, l.an), {c, "none", "2 3", 1.2, .85}, 1, 0,
                   "motion");
        d.line(P(l.ae, l.an), P(l.ae + l.ue * LL * .95, l.an + l.un * LL * .95),
               {c, "none", "6 4", .9, .75}, 1, 0, "azimuth");
        Point ap = P(l.ae, l.an), t = P(l.e, l.n);
        d.line(ap, t, {c, "none", "", 3}, 1, 0, "intercept");
        // SVG marker viewBox 0 0 10 10, refX=9/refY=5, 5.5 x 5.5
        // with markerUnits=strokeWidth. Retain its exact arrowhead dimensions.
        double dx = t.x - ap.x, dy = t.y - ap.y, len = std::hypot(dx, dy);
        if (len > 1e-12) {
            dx /= len;
            dy /= len;
            double u = 1.65;
            d.path({{t.x - 9 * u * dx + 5 * u * dy, t.y - 9 * u * dy - 5 * u * dx},
                    {t.x + u * dx, t.y + u * dy},
                    {t.x - 9 * u * dx - 5 * u * dy, t.y - 9 * u * dy + 5 * u * dx}},
                   {"none", c, "", 0}, 1, 0, true, "intercept-arrow");
        }
        d.line(P(l.e + pe * LL, l.n + pn * LL), P(l.e - pe * LL, l.n - pn * LL),
               {c, "none", "", 2.2}, 1, 0, "lop");
        d.path({{ap.x, ap.y - 5}, {ap.x + 4.5, ap.y + 3}, {ap.x - 4.5, ap.y + 3}},
               {c, "none", "", 1.4}, 1, 0, true, "assumed-position");
        d.text({ap.x + 7, ap.y + 12}, "AP " + std::to_string(i + 1), 2, 0, 9.5, c, "start", true,
               true, false, true, true);
        Point a{l.e + pe * LL * .72, l.n + pn * LL * .72},
            b{l.e - pe * LL * .72, l.n - pn * LL * .72};
        Point lp = a.x * a.x + a.y * a.y >= b.x * b.x + b.y * b.y ? a : b;
        Point q = P(lp.x, lp.y);
        std::string name =
            !std::strcmp(o.sights[i].body, "star") ? o.sights[i].star : o.sights[i].body;
        if (name != "" && std::strcmp(o.sights[i].body, "star"))
            name[0] = (char)std::toupper((unsigned char)name[0]);
        d.text({q.x, q.y - 6}, name, 2, 0, 10, c, "middle", false, false, true, true, true);
        double intercept = o.reductions[i].intercept_nm;
        Point mid = P(l.ae + l.ue * intercept * .5 + pe * .30 * span * .1,
                      l.an + l.un * intercept * .5 + pn * .30 * span * .1);
        d.text(mid, decimal(std::fabs(intercept)) + "' " + (intercept >= 0 ? "T" : "A"), 2, 0, 9.5,
               c, "middle", true, true, false, true, true);
    }
    std::vector<Point> hat;
    if (L.size() == 3) {
        for (auto pair : {std::pair<int, int>{0, 1}, {1, 2}, {2, 0}}) {
            auto &a = L[pair.first];
            auto &b = L[pair.second];
            double det = a.ue * b.un - a.un * b.ue;
            if (std::fabs(det) < 1e-9) {
                hat.clear();
                break;
            }
            double aa = a.e * a.ue + a.n * a.un, bb = b.e * b.ue + b.n * b.un;
            hat.push_back({(aa * b.un - a.un * bb) / det, (a.ue * bb - aa * b.ue) / det});
        }
        if (hat.size() == 3) {
            std::vector<Point> pts;
            for (auto &p : hat)
                pts.push_back(P(p.x, p.y));
            d.path(pts, {FIXC, FIXC, "", .9, 1, .12}, 3, 0, true, "cocked-hat");
        }
    }
    Point F = P(0, 0);
    d.circle(F, 5.5, {FIXC, "none", "", 1.8}, 3, 0, "fix-ring");
    d.circle(F, 1.6, {"none", FIXC, "", 0}, 3, 0, "fix-dot");
    d.text({F.x + 10, F.y - 7}, "FIX", 3, 0, 11, FIXC, "start", true, true, false, true, true);
    double sb = nice_interval(75 / d.scale), bx = PAD + 10, by = PAD + ph - 30, bw = sb * d.scale;
    d.rect({bx - 5, by - 19, bw + 10, 26}, {GRID, "#ffffff", "", .6, 1, .9}, 0, -1,
           "scale-backdrop");
    d.rect({bx, by - 11, bw / 2, 5}, {"none", INK, "", 0}, 0, -1, "scale-dark");
    d.rect({bx + bw / 2, by - 11, bw / 2, 5}, {INK, "#ffffff", "", .6}, 0, -1, "scale-light");
    std::ostringstream sbtext;
    sbtext << sb;
    d.text({bx + bw / 2, by + 2}, sbtext.str() + " NM", 0, -1, 9, MUT, "middle", true);
    d.text({PAD, PAD - 14}, "UNIVERSAL PLOTTING SHEET", 0, -1, 11, INK, "start", false, true);
    d.text({PAD + pw, PAD - 14}, "each sight from its own HO 229 assumed position", 0, -1, 10, MUT,
           "end", false, false, true);
    double sy = PAD + ph + 16;
    if (hat.size() == 3) {
        double bw = 196, bh = STRIP - 26, bx = W - PAD - bw, by = sy;
        double hemin = hat[0].x, hemax = hemin, hnmin = hat[0].y, hnmax = hnmin;
        for (auto &p : hat) {
            hemin = std::min(hemin, p.x);
            hemax = std::max(hemax, p.x);
            hnmin = std::min(hnmin, p.y);
            hnmax = std::max(hnmax, p.y);
        }
        double hce = (hemin + hemax) / 2, hcn = (hnmin + hnmax) / 2,
               hspan = std::max({hemax - hemin, hnmax - hnmin, .02}) * 2.3,
               hs = std::min(bw - 26, bh - 34) / hspan, hx = bx + bw / 2, hy = by + bh / 2 + 7;
        auto Q = [&](double e, double n) {
            return Point{hx + (e - hce) * hs, hy - (n - hcn) * hs};
        };
        d.clips[1] = {bx, by, bw, bh};
        d.rect(d.clips[1], {GRIDJ, "none", "", .8}, 3, -1, "detail-frame");
        for (size_t i = 0; i < L.size(); ++i) {
            auto &l = L[i];
            double ai = l.e * l.ue + l.n * l.un, tsc = ai - (hce * l.ue + hcn * l.un),
                   fe = hce + tsc * l.ue, fn = hcn + tsc * l.un;
            d.line(Q(fe + l.un * hspan, fn - l.ue * hspan), Q(fe - l.un * hspan, fn + l.ue * hspan),
                   {lop_color(i), "none", "", 1.8}, 3, 1, "detail-lop", false);
        }
        std::vector<Point> points;
        for (auto &p : hat)
            points.push_back(Q(p.x, p.y));
        d.path(points, {FIXC, FIXC, "", 1.1, 1, .15}, 3, 1, true, "detail-hat", false);
        d.circle(Q(0, 0), 4.5, {FIXC, "none", "", 1.6}, 3, 1, "detail-fix-ring", false);
        d.circle(Q(0, 0), 1.4, {"none", FIXC, "", 0}, 3, 1, "detail-fix-dot", false);
        std::ostringstream factor;
        factor << std::setprecision(2) << hs / d.scale;
        std::ostringstream factor_label;
        factor_label << std::stod(factor.str());
        d.text({bx + 7, by + 14}, "COCKED HAT ×" + factor_label.str(), 3, -1, 9, MUT, "start", true,
               true);
    }
    std::vector<std::string> footer = {
        "Longitude scale uses cos " + decimal(std::fabs(o.grid_lat)) + "° = " + decimal(CL, 3) +
            ".",
        "Grid intervals: latitude " + decimal(step, step < 1 ? 1 : 0) + "', longitude " +
            decimal(lonStep, lonStep < 1 ? 1 : 0) + "'.",
        "",
        "Every sight keeps its own HO 229 assumed position. The intercept",
        "is stepped toward the body when Ho is the greater and away when",
        "it is the less, then the LOP is drawn square to the azimuth."};
    for (size_t i = 0; i < footer.size(); ++i)
        if (!footer[i].empty())
            d.text({PAD, sy + 14 + i * 15}, footer[i], 0, -1, 9.5, MUT);
    return d;
}
inline std::string svg(const Overlay &o, const bool visible[4], double opacity) {
    auto d = make_sheet(o);
    std::ostringstream out;
    out << std::setprecision(12);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" aria-label=\"Celestial fix "
           "plotting sheet\" viewBox=\"0 0 760 772\">\n<title>NAVALM Universal Plotting "
           "Sheet</title>\n";
    out << "<metadata>Transparent vector sheet; origin_lat=" << o.lat << "; origin_lon=" << o.lon
        << "; scale_lat=" << o.grid_lat << "; centre_e_nm=" << d.ce << "; centre_n_nm=" << d.cn
        << "; px_per_nm=" << d.scale
        << ". Main panel is geographic; inset is an enlarged detail. Reproject geographic vectors "
           "for chart use.</metadata>\n<defs>";
    for (int i = 0; i < 2; ++i) {
        auto b = d.clips[i];
        out << "<clipPath id=\"sheet" << i << "\"><rect x=\"" << b.x << "\" y=\"" << b.y
            << "\" width=\"" << b.w << "\" height=\"" << b.h << "\"/></clipPath>";
    }
    out << "</defs>\n<g opacity=\"" << opacity << "\">\n";
    const char *names[] = {"graticule", "lines-of-position", "labels", "fix-and-detail"};
    for (auto item : d.order) {
        int layer = item.first ? d.captions[item.second].layer : d.marks[item.second].layer;
        if (!visible[layer])
            continue;
        // Repeated named classes keep paint order and allow SVG consumers to
        // toggle every fragment of a layer with one CSS selector.
        out << "<g class=\"" << names[layer] << "\">\n";
        if (!item.first) {
            const auto &p = d.marks[item.second];
            out << "<" << (p.closed ? "polygon" : "polyline") << " data-role=\"" << p.role
                << "\" fill=\"" << p.ink.fill << "\" stroke=\"" << p.ink.stroke
                << "\" stroke-width=\"" << p.ink.width << "\" opacity=\"" << p.ink.alpha
                << "\" fill-opacity=\"" << p.ink.fill_alpha << "\"";
            if (p.clip >= 0)
                out << " clip-path=\"url(#sheet" << p.clip << ")\"";
            if (!p.ink.dash.empty())
                out << " stroke-dasharray=\"" << p.ink.dash << "\"";
            out << " points=\"";
            for (auto q : p.points)
                out << q.x << "," << q.y << " ";
            out << "\"/>\n";
        } else {
            const auto &l = d.captions[item.second];
            auto text = [&](bool outline) {
                out << "<text x=\"" << l.point.x << "\" y=\"" << l.point.y << "\" font-size=\""
                    << l.size << "\" text-anchor=\"" << l.anchor << "\" font-family=\""
                    << (l.mono ? "ui-monospace,Menlo,Consolas,monospace" : "system-ui,sans-serif")
                    << "\" font-weight=\"" << (l.bold ? "bold" : "normal") << "\"";
                if (l.italic)
                    out << " font-style=\"italic\"";
                if (l.clip >= 0)
                    out << " clip-path=\"url(#sheet" << l.clip << ")\"";
                if (outline)
                    out << " fill=\"none\" stroke=\"#ffffff\" stroke-width=\"3.5\" "
                           "stroke-linejoin=\"round\"";
                else
                    out << " fill=\"" << l.color << "\"";
                out << ">" << xml(l.text) << "</text>\n";
            };
            if (l.halo)
                text(true);
            text(false);
        }
        out << "</g>\n";
    }
    out << "</g></svg>\n";
    return out.str();
}
inline std::string json_text(const std::string &s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 32) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else
            out += (char)c;
    }
    return out;
}
inline std::string geojson(const Overlay &o) {
    auto d = make_sheet(o);
    std::ostringstream out;
    out << std::setprecision(12) << "{\"type\":\"FeatureCollection\",\"features\":[";
    bool first = true;
    for (auto &p : d.marks)
        if (p.map_geometry) {
            if (!first)
                out << ",";
            first = false;
            out << "{\"type\":\"Feature\",\"properties\":{\"layer\":" << p.layer << ",\"role\":\""
                << p.role << "\",\"color\":\"" << p.ink.stroke << "\",\"fill\":\"" << p.ink.fill
                << "\",\"width\":" << p.ink.width << ",\"dash\":\"" << p.ink.dash
                << "\",\"opacity\":" << p.ink.alpha << ",\"fill-opacity\":" << p.ink.fill_alpha
                << "},\"geometry\":{\"type\":\"" << (p.closed ? "Polygon" : "LineString")
                << "\",\"coordinates\":[";
            if (p.closed)
                out << "[";
            bool start = true;
            auto point = [&](Point v) {
                if (!start)
                    out << ",";
                start = false;
                auto g = d.geographic(o, v);
                out << "[" << g.lon << "," << g.lat << "]";
            };
            for (auto q : p.points)
                point(q);
            if (p.closed)
                point(p.points.front());
            if (p.closed)
                out << "]";
            out << "]}}";
        }
    for (auto &l : d.captions)
        if (l.map_geometry) {
            if (!first)
                out << ",";
            first = false;
            auto g = d.geographic(o, l.point);
            out << "{\"type\":\"Feature\",\"properties\":{\"layer\":" << l.layer << ",\"text\":\""
                << json_text(l.text) << "\",\"color\":\"" << l.color
                << "\"},\"geometry\":{\"type\":\"Point\",\"coordinates\":[" << g.lon << "," << g.lat
                << "]}}";
        }
    out << "]}\n";
    return out.str();
}
} // namespace desktop
#endif
