/* SPDX-License-Identifier: MIT
 * Independently calculated NAVALM annual practice books. No scanned pages,
 * publisher tables, or publisher explanatory text are embedded.
 */
#include "navbook.h"
#include "navbook_lm_metrics.h"
#include "navplatform.h"
extern "C" {
#include "nav_engine.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace navbook {
static constexpr double pi = 3.14159265358979323846;
static double sn(double x) { return std::sin(x * pi / 180); }
static double cs(double x) { return std::cos(x * pi / 180); }
static double asn(double x) { return std::asin(std::clamp(x, -1., 1.)) * 180 / pi; }
static double wrap(double x) { x = std::fmod(x, 360); return x < 0 ? x + 360 : x; }
static double signed_angle(double x) { return wrap(x + 180) - 180; }
static std::string num(double x, int places = 1) {
    if(std::abs(x)<.5/std::pow(10.,places)) x=0;
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(places) << x; return s.str();
}
static std::string angle(double x, bool sign = false, int places = 1) {
    double units = std::pow(10., places);
    long ticks = std::lround(std::abs(x) * 60 * units);
    int d = int(ticks / long(60 * units));
    double m = (ticks % long(60 * units)) / units;
    char b[48];
    std::snprintf(b, sizeof b, "%s%0*d %0*.*f", sign ? (x < 0 ? "S" : "N") : "",
                  sign ? 2 : 3, sign ? d : d % 360, places + 3, places, m);
    return b;
}

Ageton::Ageton() {
    a_.resize(10801);
    a_[0] = std::numeric_limits<double>::infinity();
    for (int i = 1; i < 10800; ++i)
        a_[i] = std::round(-scale * std::log10(sn(i * step)));
    a_.back() = 0;
}
double Ageton::a(double x, bool interpolate) const {
    x = std::abs(signed_angle(x));
    if (x > 90) x = 180 - x;
    double index = x / step;
    int lo = std::clamp(int(std::floor(index + 1e-9)), 0, 10800);
    if (!interpolate) return a_[std::clamp(int(std::floor(index + .5)), 0, 10800)];
    if (lo == 10800 || index - lo < 1e-8) return a_[lo];
    if (lo == 0) return std::numeric_limits<double>::infinity();
    return a_[lo] + (a_[lo + 1] - a_[lo]) * (index - lo);
}
double Ageton::b(double x, bool interpolate) const { return a(90 - x, interpolate); }
double Ageton::inverse_a(double value, bool interpolate) const {
    if (std::isinf(value) && value > 0) return 0;
    if (!std::isfinite(value) || value < 0)
        throw std::runtime_error("Negative or invalid logarithm: check arithmetic or use another method.");
    auto it = std::lower_bound(a_.begin() + 1, a_.end(), value, std::greater<double>());
    int hi = int(it - a_.begin());
    if (hi == 1 && value > a_[1])
        throw std::runtime_error("Angle smaller than first table interval.");
    if (hi >= 10801) return 90;
    if (*it == value) return hi * step;
    int lo = hi - 1;
    if (interpolate && a_[lo] != a_[hi])
        return (lo + (a_[lo] - value) / (a_[lo] - a_[hi])) * step;
    return (std::abs(a_[lo] - value) < std::abs(a_[hi] - value) ? lo : hi) * step;
}
double Ageton::inverse_b(double value, bool interpolate) const {
    return 90 - inverse_a(value, interpolate);
}
Ageton::Result Ageton::reduce(double lat, double dec, double lha, bool interpolate) const {
    Result out;
    try {
        if (!std::isfinite(lat) || !std::isfinite(dec) || !std::isfinite(lha) ||
            std::abs(lat) >= 90 || std::abs(dec) >= 90)
            throw std::runtime_error("Invalid coordinates or polar boundary.");
        lha = wrap(lha);
        double t = lha <= 180 ? lha : 360 - lha;
        if (std::abs(sn(t)) < 1e-8 || std::abs(cs(t)) < 1e-8 || std::abs(dec) < step)
            throw std::runtime_error("Meridian, prime-vertical, or equatorial boundary: use direct formulas.");
        double ar = a(t, interpolate) + b(dec, interpolate);
        out.r = inverse_a(ar, interpolate);
        if (out.r > 87)
            throw std::runtime_error("R above 87 degrees: ill-conditioned Ageton triangle.");
        double br = b(out.r, interpolate);
        double aq = a(dec, interpolate) - br;
        // Use the well-conditioned inverse: A for acute angles <=45, B above45.
        double q = aq >= a(45) ? inverse_a(aq, interpolate)
                              : inverse_b(b(dec, interpolate) + b(t, interpolate) - br, interpolate);
        out.q = (dec < 0 ? -1 : 1) * (t < 90 ? q : 180 - q);
        double gap = signed_angle(out.q - lat);
        if (std::abs(gap) >= 90)
            throw std::runtime_error("Body below the horizon.");
        out.altitude = inverse_a(br + b(gap, interpolate), interpolate);
        if (out.altitude < 5 || out.altitude > 85)
            throw std::runtime_error("Hc outside 5 to 85 degrees: use another method.");
        double bh = b(out.altitude, interpolate);
        double az = ar - bh;
        double z = az >= a(45) ? inverse_a(az, interpolate)
                              : inverse_b(br + a(gap, interpolate) - bh, interpolate);
        if (gap < 0) z = 180 - z;
        out.azimuth = lha < 180 ? wrap(360 - z) : z;
        out.usable = true;
    } catch (const std::exception &e) { out.reason = e.what(); }
    return out;
}

// Shared page layout with native PDF and optional standalone LaTeX backends.
// Direct PDF export requires no TeX installation.
class PDF {
    Format format;
    std::vector<std::string> pages;
    std::ostringstream page;
    std::vector<std::pair<std::string, int>> contents;
    static std::string escaped(const std::string &s) {
        std::string r;
        for (unsigned char c : s) {
            if (c == '(' || c == ')' || c == '\\') r += '\\';
            if (c >= 32 && c <= 126) r += char(c);
        }
        return r;
    }
  public:
    static constexpr double width = 504, height = 720;
    explicit PDF(Format f) : format(f) { page.imbue(std::locale::classic()); }
    static std::string tex_escape(const std::string &s) {
        std::string out;
        for(unsigned char c:s) {
            switch(c) {
                case '\\': out+="\\textbackslash{}";break;
                case '{': case '}': case '$': case '&': case '#': case '%': case '_':
                    out+='\\';out+=char(c);break;
                case '^':out+="\\textasciicircum{}";break;
                case '~':out+="\\textasciitilde{}";break;
                case ' ':out+="\\ ";break;
                case '-':out+="{-}";break; // Keep literal '--' event placeholders.
                default:if(c>=32 && c<=126)out+=char(c);
            }
        }
        return out;
    }
    double text_width(const std::string &s, double size=8, int font=0) const {
        // Adobe standard-font advances, in thousandths of an em. Numeric
        // columns use tabular Times digits; prose wraps at its measured width.
        static const int roman[]={250,333,408,500,500,833,778,180,333,333,500,564,250,333,250,278,500,500,500,500,500,500,500,500,500,500,278,278,564,564,564,444,921,722,667,667,722,611,556,722,722,333,389,722,611,889,722,722,556,722,667,556,611,722,722,944,722,722,611,333,278,333,469,500,333,444,500,444,500,444,333,500,500,278,278,500,278,778,500,500,500,500,333,389,278,500,500,722,500,500,444,480,200,480,541};
        static const int bold[]={250,333,555,500,500,1000,833,278,333,333,500,570,250,333,250,278,500,500,500,500,500,500,500,500,500,500,333,333,570,570,570,500,930,722,667,722,722,667,611,778,778,389,500,778,667,944,722,778,611,778,722,556,667,722,722,1000,722,722,667,333,278,333,581,500,333,500,556,444,556,444,333,500,556,278,333,556,278,833,556,500,556,556,444,389,333,556,500,722,500,500,444,394,220,394,520};
        static const int helvetica[]={278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,556,500,722,500,500,500,334,260,334,584};
        double units=0;
        for(unsigned char c:s) if(c>=32 && c<=126)
            units+=format==Format::Latex?navbook_lm_widths[font][c-32]:
                (font==2?600:(font==1?bold[c-32]:(font==3?helvetica[c-32]:roman[c-32])));
        return units*size/1000.;
    }
    void right(double edge, double y, const std::string &s, double size=8, int font=0) {
        text(edge-text_width(s,size,font),y,s,size,font);
    }
    void fit_text(double x, double y, const std::string &s, double space,
                  double size=8, int font=0) {
        text(x,y,s,std::min(size,space/text_width(s,1,font)),font);
    }
    void text(double x, double y, const std::string &s, double size = 8, int font = 0) {
        if (x < 20 || x + text_width(s,size,font) > width - 20 || y < 20 || y > height - 20)
            throw std::runtime_error("Book layout outside printable margins: "+s);
        if(format==Format::Latex) {
            page << "\\NavText{" << x << "}{" << height-y << "}{" << size << "}{" << font
                 << "}{" << tex_escape(s) << "}\n";
            return;
        }
        page << "BT /F" << font << ' ' << size << " Tf 1 0 0 1 " << x << ' ' << height - y
             << " Tm (" << escaped(s) << ") Tj ET\n";
    }
    void line(double x, double y, double w, double weight = .4) {
        if(format==Format::Latex) {
            page << "\\put(" << x << ',' << height-y-weight/2 << "){\\rule{" << w << "bp}{" << weight << "bp}}\n";
            return;
        }
        page << weight << " w " << x << ' ' << height - y << " m " << x + w << ' '
             << height - y << " l S\n";
    }
    void shade_row(double x, double baseline, double w, double row_height=7.5, double below=1.5) {
        // Eight percent black, limited to the table's own columns.
        // Draw before text; isolate the fill so text and rules remain black.
        if(format==Format::Latex) {
            page << "\\put(" << x << ',' << height-baseline-below
                 << "){\\textcolor{navrow}{\\rule{" << w << "bp}{" << row_height << "bp}}}\n";
        } else {
            page << "q 0.92 g " << x << ' ' << height-baseline-below << ' '
                 << w << ' ' << row_height << " re f Q\n";
        }
    }
    void striped_rows(double x, double baseline, double w, double step, int count, double below) {
        // Paint all backgrounds first so following rules stay visible.
        for(int row=1;row<count;row+=2) shade_row(x,baseline+row*step,w,step,below);
    }
    void start(const std::string &title, const std::string &section = "") {
        if (!page.str().empty()) finish();
        if (!section.empty()) contents.emplace_back(section, int(pages.size()) + 1);
        text(24, 33, title, 11, 1); line(24, 42, 456, .8);
    }
    void mark(const std::string &label) { contents.emplace_back(label, int(pages.size())+1); }
    void finish() {
        text(24, 697, "NAVALM | Practice only - use the official almanac for navigation", 7);
        text(458, 697, std::to_string(pages.size() + 1), 8);
        pages.push_back(page.str()); page.str(""); page.clear();
    }
    void paragraph(double x, double &y, const std::string &s, double max_width = 0,
                   double size = 10, double leading = 13) {
        if(max_width<=0) max_width=width-24-x;
        std::istringstream in(s); std::string word, row;
        while (in >> word) {
            if (text_width(row+" "+word,size) > max_width && !row.empty()) {
                text(x, y, row, size); y += leading; row.clear();
            }
            if (!row.empty()) row += ' ';
            row += word;
        }
        if (!row.empty()) { text(x, y, row, size); y += leading; }
        y += 7;
    }
    std::string document() {
        auto index=contents;
        start("SECTION INDEX");
        double iy=70;
        for(const auto &entry:index) {
            text(24,iy,entry.first,9); text(452,iy,std::to_string(entry.second),9); iy+=24;
        }
        if (!page.str().empty()) finish();
        if(format==Format::Latex) {
            std::ostringstream out; out.imbue(std::locale::classic());
            out << R"TEX(% Generated by NAVALM. Edit freely; regenerate to refresh calculated data.
% Compile twice: pdflatex -interaction=nonstopmode -halt-on-error main.tex
\documentclass[10pt]{article}
\usepackage[T1]{fontenc}
\usepackage{lmodern}
\usepackage{xcolor}
\definecolor{navrow}{gray}{0.92}
\usepackage[paperwidth=504bp,paperheight=720bp,margin=0bp]{geometry}
\usepackage[hidelinks,bookmarksopen]{hyperref}
% Pin the design sizes to the advances used by NAVALM's layout engine.
\DeclareFontFamily{T1}{lmr}{}
\DeclareFontFamily{T1}{lmtt}{}
\DeclareFontFamily{T1}{lmss}{}
\DeclareFontShape{T1}{lmr}{m}{n}{<->ec-lmr10}{}
\DeclareFontShape{T1}{lmr}{bx}{n}{<->ec-lmbx10}{}
\DeclareFontShape{T1}{lmtt}{m}{n}{<->ec-lmtt10}{}
\DeclareFontShape{T1}{lmss}{m}{n}{<->ec-lmss10}{}
\newcommand{\NavText}[5]{\put(#1,#2){%
  \ifcase#4\fontfamily{lmr}\fontseries{m}%
  \or\fontfamily{lmr}\fontseries{bx}%
  \or\fontfamily{lmtt}\fontseries{m}%
  \or\fontfamily{lmss}\fontseries{m}\fi
  \fontsize{#3bp}{#3bp}\selectfont #5}}
\pagestyle{empty}
\setlength{\parindent}{0pt}
\setlength{\topskip}{0pt}
\setlength{\unitlength}{1bp}
\hypersetup{pdftitle={NAVALM annual practice almanac},pdfauthor={NAVALM}}
\begin{document}
)TEX";
            for(size_t i=0;i<pages.size();++i) {
                out << "% NAVALM page " << i+1 << "\n";
                for(size_t j=0;j<contents.size();++j) if(contents[j].second==int(i+1))
                    out << "\\pdfbookmark[0]{" << tex_escape(contents[j].first) << "}{navalm-" << j << "}\n";
                out << "\\noindent\\begin{picture}(504,720)(0,0)\n" << pages[i] << "\\end{picture}\\par\n";
                if(i+1<pages.size()) out << "\\newpage\n";
            }
            out << "\\end{document}\n";
            return out.str();
        }
        std::vector<std::string> objects;
        int outline_root=int(7+2*pages.size());
        objects.emplace_back("<< /Type /Catalog /Pages 2 0 R /Outlines "+std::to_string(outline_root)+" 0 R >>");
        std::string kids;
        for (size_t i = 0; i < pages.size(); ++i) kids += std::to_string(7 + 2*i) + " 0 R ";
        objects.emplace_back("<< /Type /Pages /Count " + std::to_string(pages.size()) +
                             " /Kids [" + kids + "] >>");
        for (auto font : {"Times-Roman", "Times-Bold", "Courier", "Helvetica"})
            objects.emplace_back(std::string("<< /Type /Font /Subtype /Type1 /BaseFont /") + font + " >>");
        for (size_t i = 0; i < pages.size(); ++i) {
            objects.emplace_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 504 720] "
                "/Resources << /Font << /F0 3 0 R /F1 4 0 R /F2 5 0 R /F3 6 0 R >> >> "
                "/Contents " + std::to_string(8 + 2*i) + " 0 R >>");
            objects.emplace_back("<< /Length " + std::to_string(pages[i].size()) +
                                 " >>\nstream\n" + pages[i] + "endstream");
        }
        objects.emplace_back("<< /Type /Outlines /Count "+std::to_string(contents.size())+
            " /First "+std::to_string(outline_root+1)+" 0 R /Last "+
            std::to_string(outline_root+contents.size())+" 0 R >>");
        for(size_t i=0;i<contents.size();++i) {
            std::string entry="<< /Title ("+escaped(contents[i].first)+") /Parent "+std::to_string(outline_root)+
                " 0 R /Dest ["+std::to_string(7+(contents[i].second-1)*2)+" 0 R /Fit] ";
            if(i) entry+="/Prev "+std::to_string(outline_root+i)+" 0 R ";
            if(i+1<contents.size()) entry+="/Next "+std::to_string(outline_root+i+2)+" 0 R ";
            objects.push_back(entry+">>");
        }
        std::ostringstream out; out.imbue(std::locale::classic());
        out << "%PDF-1.4\n%NAVALM\n";
        std::vector<long long> offsets{0};
        for (size_t i = 0; i < objects.size(); ++i) {
            offsets.push_back(static_cast<long long>(out.tellp()));
            out << i + 1 << " 0 obj\n" << objects[i] << "\nendobj\n";
        }
        auto xref = out.tellp();
        out << "xref\n0 " << objects.size()+1 << "\n0000000000 65535 f \n";
        for (size_t i = 1; i < offsets.size(); ++i)
            out << std::setfill('0') << std::setw(10) << offsets[i] << " 00000 n \n";
        out << "trailer\n<< /Size " << objects.size()+1 << " /Root 1 0 R >>\nstartxref\n"
            << xref << "\n%%EOF\n";
        return out.str();
    }
};

struct Day {
    int month, day;
    double jd;
    std::array<std::array<NavAlmanac, 25>, 7> body{};
};
static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
static bool leap(int y) { return y%4 == 0 && (y%100 || y%400 == 0); }
static std::vector<Day> calendar(int year) {
    std::vector<Day> result;
    for (int m = 1; m <= 12; ++m)
        for (int d = 1; d <= lengths[m-1] + (m == 2 && leap(year)); ++d)
            result.push_back({m, d, nav_julian_date(year,m,d,0,0,0), {}});
    return result;
}
static const char *months[] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
    "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
static std::string date(const Day &d) { return std::string(months[d.month-1]) + " " + std::to_string(d.day); }
static void collect(Day &d, const Options &o) {
    for (int b = 0; b < 7; ++b) {
        if (b == NAV_MERCURY) continue;
        if (b < 2 && !(o.sun_moon || o.events)) continue;
        if (b >= NAV_VENUS && !o.planets) continue;
        for (int h = 0; h <= 24; ++h) {
            auto &v = d.body[b][h];
            if (nav_almanac(static_cast<NavBody>(b), d.jd + h/24., &v) ||
                !std::isfinite(v.gha_deg) || !std::isfinite(v.dec_deg))
                throw std::runtime_error("Ephemeris calculation failed: " + std::string(nav_engine_last_error()));
        }
    }
}
static void opening(PDF &p, const Options &o) {
    p.start("NAVALM CUSTOM NAUTICAL ALMANAC", "Title and conventions");
    p.text(24, 100, std::to_string(o.year), 42, 1);
    p.text(24, 140, "Annual practice edition | 7 x 10 inch pages", 13);
    double y = 180;
    p.paragraph(24,y,"An independently computed book for learning and recreational practice. "
        "Not an official USNO or HMNAO publication. For real navigation, purchase and use the current "
        "official Nautical Almanac and appropriate authorized charts and procedures.");
    p.paragraph(24,y,"No warranty of correctness or fitness is provided. Do not rely on this book "
        "where an error could endanger life or property. See NAVALM SAFETY.md and LICENSE for "
        "the software terms; no claim is made that a disclaimer removes legal responsibilities.");
    p.paragraph(24,y,"Astronomy: " + std::string(nav_engine_name()) + ". " + nav_engine_detail());
    p.paragraph(24,y,"Time: whole UTC hours, with DUT1 assumed zero. Future leap seconds and "
        "Earth-rotation predictions are uncertain. Printed digits are resolution, not an accuracy "
        "guarantee. Values are apparent geocentric coordinates, rounded to 0.1 minute of arc.");
    p.paragraph(24,y,"Angles are printed as degrees SPACE minutes, e.g. 123 45.6. Declinations "
        "carry N or S on every row. Longitudes are east positive; LHA = GHA + east longitude, "
        "reduced to 0 through 360 degrees. GHA and SHA increase westward.");
    p.paragraph(24,y,"The selectable sections are listed on the next page. This development edition "
        "does not reproduce every official-book section: NAO Concise tables, Polaris correction "
        "tables, eclipse predictions, star charts and standard-time maps are not included.");
    p.start("CONTENTS AND USE");
    y = 70;
    for (auto entry : std::vector<std::pair<bool,std::string>>{
        {o.corrections,"Front reference: dip, refraction and parallax corrections"},
        {o.planets,"Three-day Aries and navigational-planet pages"},
        {o.stars,"Navigational-star SHA and declination on three-day pages"},
        {o.sun_moon,"Three-day Sun and Moon hourly pages, v/d, HP and SD"},
        {o.events,"Greenwich rise/set and twilight tables alongside daily pages"},

        {o.reduction == Reduction::Formulas || o.reduction == Reduction::Both,"Concise calculator formulas and worksheet"},
        {o.reduction == Reduction::Ageton || o.reduction == Reduction::Both,"Compact Ageton tables, instructions and worksheet (25 pages)"},
        {o.increments,"Back reference: increments and corrections for minutes/seconds"}}) {
        p.text(24,y, std::string(entry.first ? "INCLUDED   " : "OMITTED    ") + entry.second, 9);
        y += 23;
    }
    y += 15;
    p.paragraph(24,y,"Daily pages group three consecutive calendar days. Each day has all 24 hours. "
        "The final group stops on December 31; no next-year daily pages are inserted. Adjacent "
        "next-hour ephemerides are used for the final hourly rates. Planet pages and Sun/Moon "
        "pages are omitted independently; selecting stars alone still creates the Aries/star page. "
        "Day and UTC hour are separate columns. Use Increments and Corrections at the back "
        "to advance a whole-hour GHA to the observation's minute and second.");
    p.paragraph(24,y,"v is the signed excess hourly GHA rate over the base motion: 15 degrees/hour "
        "for Sun and planets, 14 degrees 19 minutes/hour for Moon. d is the signed change "
        "in declination in minutes/hour (N positive). Unlike the official book, all d rates here "
        "are explicitly signed. Use the signed rate, not a change in the printed N/S magnitude.");
    p.paragraph(24,y,"Greenwich event times are computed at longitude zero, sea level and a level "
        "horizon. Solar twilight uses center altitudes -6 and -12 degrees; rise/set uses 34 minutes "
        "of refraction and the body semidiameter plus parallax. These are approximate times, "
        "rounded to one minute. '--' means no event in that UTC day. They are not exact local "
        "times for other longitudes; recompute there, especially for Moon or polar events.");
    p.paragraph(24,y,"Use the sight-reduction method checkboxes independently. The concise calculator "
        "guide is NAVALM's own formula sheet, not the NAO Concise paper tables. Ageton may be "
        "included with it, substituted for it, or left out. The Ageton scale is 1,000,000; never "
        "mix these entries with the historical 100,000-scale tables.");
}

static std::string hour(int h) { char s[8]; std::snprintf(s,sizeof s,"%02d",h); return s; }
static void planets_page(PDF &p, const std::vector<Day> &days, size_t first, size_t end, const Options &o) {
    p.start(std::to_string(o.year) + " | " + date(days[first]) + " - " + date(days[end-1]));
    for(size_t i=first;i<end;++i) if(days[i].day==1) p.mark(std::string(months[days[i].month-1])+" daily pages");
    const double aries=o.planets && o.stars?48:52, body_x=o.stars?88:110;
    const double stride=o.stars?64:94, face=o.planets && o.stars?7.5:8.5;
    p.text(24,55,"UTC",7,1); p.right(34,66,"Day",5,1); p.text(36,66,"h",7,1);
    p.text(aries,54,"ARIES",7,1); p.text(aries,66,"GHA",7,1);
    for (int b=0;b<4 && o.planets;++b) {
        double x=body_x+b*stride;
        p.text(x,54,nav_body_name(static_cast<NavBody>(b+NAV_VENUS)),8,1);
        p.text(x,66,"GHA",7,1); p.text(x+stride/2,66,"DEC",7,1);
    }
    double y=78;
    for(size_t i=first;i<end;++i) {
        p.line(24,y-6,o.planets?(o.stars?318:456):90);
        for(int h=0;h<24;++h) {
            if(o.shade_rows && h%2) p.shade_row(24,y,o.planets?(o.stars?318:456):90);
            if(h==0) p.text(24,y,std::to_string(days[i].day),7,1);
            p.text(36,y,hour(h),7);
            p.text(aries,y,angle(nav_gha_aries_deg(days[i].jd+h/24.)),face);
            for(int b=0;b<4 && o.planets;++b) {
                const auto &v=days[i].body[b+NAV_VENUS][h];
                p.text(body_x+b*stride,y,angle(v.gha_deg),face);
                p.text(body_x+b*stride+stride/2,y,angle(v.dec_deg,true),face);
            }
            y+=7.5;
        }
    }
    if(o.planets) {
        y=626;
        for(int b=0;b<4;++b) {
            double x=24+b*(o.stars?81:115);
            p.text(x,y,nav_body_name(static_cast<NavBody>(b+NAV_VENUS)),8,1);
            for(size_t i=first;i<end;++i) {
                auto a=days[i].body[b+NAV_VENUS][12], n=days[i].body[b+NAV_VENUS][13];
                p.text(x,y+12+(i-first)*12,std::to_string(days[i].day)+": v "+num((wrap(n.gha_deg-a.gha_deg)-15)*60)+
                    " d "+num((n.dec_deg-a.dec_deg)*60),7.5);
            }
        }
        p.text(24,680,"Rates by day at 12-13 UTC; v and d in arcminutes/hour.",7);
    }
    if(o.stars) {
        const double name_x=o.planets?351:185, sha_x=o.planets?401:345, dec_x=o.planets?442:414;
        const double star_face=o.planets?7.5:9;
        p.line(name_x,69,480-name_x);
        p.text(name_x,54,"STARS | middle date 00 UTC",o.planets?7:9,1);
        p.text(name_x,66,"Name",7,1); p.text(sha_x,66,"SHA",7,1); p.text(dec_x,66,"Dec",7,1);
        double sy=78;
        for(size_t s=0;s<nav_star_count_get();++s) {
            NavStarAlmanac v{};
            if(nav_star_almanac(s,days[std::min(first+1,end-1)].jd,&v))
                throw std::runtime_error("Star calculation failed.");
            std::string name=v.name;
            p.fit_text(name_x,sy,o.planets?name.substr(0,13):name,sha_x-name_x-3,o.planets?7.2:9);
            p.text(sha_x,sy,angle(v.sha_deg),star_face);
            p.text(dec_x,sy,angle(v.dec_deg,true),star_face);
            sy+=8.3;
        }
        if(o.planets) {
            p.text(351,594,"PLANETS | noon UTC",7,1);
            p.text(351,606,"Name",7,1); p.text(401,606,"SHA",7,1); p.text(442,606,"Transit",7,1);
            const auto &day=days[std::min(first+1,end-1)];
            for(int b=0;b<4;++b) {
                const auto &body=day.body[b+NAV_VENUS];
                std::string transit="--";
                for(int h=0;h<24;++h) if(body[h+1].gha_deg<body[h].gha_deg) {
                    double f=(360-body[h].gha_deg)/wrap(body[h+1].gha_deg-body[h].gha_deg);
                    int minute=int(std::lround((h+f)*60));
                    transit=hour(minute/60)+hour(minute%60); break;
                }
                p.text(351,619+b*13,nav_body_name(static_cast<NavBody>(b+NAV_VENUS)),7.5);
                p.text(401,619+b*13,angle(wrap(body[12].gha_deg-nav_gha_aries_deg(day.jd+.5))),7.5);
                p.text(442,619+b*13,transit,7.5);
            }
        }
    }
}

static NavAlmanac sample(const Day &d, int b, double h) {
    int lo = std::clamp(int(h),0,23);
    double f = h-lo;
    auto a = d.body[b][lo], n = d.body[b][lo+1];
    a.gha_deg = wrap(a.gha_deg + signed_angle(n.gha_deg-a.gha_deg)*f);
    a.dec_deg += (n.dec_deg-a.dec_deg)*f;
    a.hp_deg += (n.hp_deg-a.hp_deg)*f; a.sd_deg += (n.sd_deg-a.sd_deg)*f;
    return a;
}
static double event_alt(const Day &d, int b, double lat, double h, double target) {
    auto a = sample(d,b,h);
    double altitude = asn(sn(lat)*sn(a.dec_deg)+cs(lat)*cs(a.dec_deg)*cs(a.gha_deg));
    if (target == 0) target = a.hp_deg - a.sd_deg - 34./60;
    return altitude-target;
}
static std::string event_time(const Day &d, int b, double lat, double target, bool rising) {
    double last = event_alt(d,b,lat,0,target);
    for (int i = 1; i <= 144; ++i) {
        double h = i/6.; double next = event_alt(d,b,lat,h,target);
        if (rising ? (last < 0 && next >= 0) : (last >= 0 && next < 0)) {
            double lo=h-1./6, hi=h;
            for (int j = 0; j < 22; ++j) {
                double mid = (lo+hi)/2, v = event_alt(d,b,lat,mid,target);
                if (rising ? v < 0 : v >= 0) lo=mid; else hi=mid;
            }
            int minutes = int(std::lround((lo+hi)/2*60));
            char s[12]; std::snprintf(s,sizeof s,"%02d%02d",minutes/60,minutes%60);
            return s;
        }
        last=next;
    }
    return " -- ";
}
static void events(PDF &p, const std::vector<Day> &days, size_t first, size_t end, bool alongside) {
    double left = alongside ? 289 : 24;
    if (!alongside) p.start("GREENWICH EVENTS | " + date(days[first]) + " - " + date(days[end-1]));
    const double scale=alongside?1:1.25, leading=alongside?7.5:15;
    auto cell=[&](int col,double y,const std::string &v,bool heading=false) {
        p.text(left+(col?22+(col-1)*24:0)*scale,y,v,heading?(alongside?7:8):(alongside?7.5:9.5),heading?1:0);
    };
    p.text(left,55,"SUN: "+date(days[std::min(first+1,end-1)])+" | UTC HHMM",7,1);
    int c=0; for(auto v:{"Lat","Ntwl","Ctwl","Rise","Set","Ctwl","Ntwl"}) cell(c++,67,v,true);
    std::vector<int> latitudes;
    for(int l=72;l>=50;l-=2) latitudes.push_back(l);
    for(int l=45;l>=-60;l-=5) latitudes.push_back(l);
    double y=78;
    for(int l:latitudes) {
        char label[12]; std::snprintf(label,sizeof label,"%02d%c",std::abs(l),l<0?'S':'N');
        cell(0,y,label); c=1;
        const auto &mid=days[std::min(first+1,end-1)];
        for(auto v:{std::pair<double,bool>{-12,true},{-6,true},{0,true},{0,false},{-6,false},{-12,false}})
            cell(c++,y,event_time(mid,NAV_SUN,l,v.first,v.second));
        y+=leading;
    }
    if(alongside) y+=16; else {left=256;y=55;}
    p.text(left,y,"MOON: RISE / SET | UTC HHMM",7,1); y+=12;
    cell(0,y,"Lat",true);
    for(size_t i=first;i<end;++i) cell(1+2*int(i-first),y,"Day "+std::to_string(days[i].day),true);
    y+=12;
    for(int l:latitudes) {
        char label[12]; std::snprintf(label,sizeof label,"%02d%c",std::abs(l),l<0?'S':'N');
        cell(0,y,label); c=1;
        for(size_t i=first;i<end;++i) {
            cell(c++,y,event_time(days[i],NAV_MOON,l,0,true));
            cell(c++,y,event_time(days[i],NAV_MOON,l,0,false));
        }
        y+=leading;
    }
    p.text(left,650,"-- : no crossing in this UTC day. Sea level.",7);
    p.text(left,663,"Greenwich longitude only; see conventions.",7);
}

static void sun_moon_page(PDF &p, const std::vector<Day> &days, size_t first, size_t end, const Options &o) {
    p.start(std::to_string(o.year)+" | "+date(days[first])+" - "+date(days[end-1]));
    if(!o.planets && !o.stars) for(size_t i=first;i<end;++i)
        if(days[i].day==1) p.mark(std::string(months[days[i].month-1])+" daily pages");
    p.text(24,55,"UTC",7,1); p.text(o.events?47:73.45,55,"SUN",8,1); p.text(o.events?116:221.8,55,"MOON",8,1);
    double field_x[]={24,47,80,116,149,182,204,227};
    if(!o.events) for(auto &x:field_x) x=24+(x-24)*2.15;
    const char *field_names[]={"","GHA","DEC","GHA","DEC","HP","v","d"};
    p.right(34,67,"Day",5,1); p.text(36,67,"h",7,1);
    for(int f=1;f<8;++f) p.text(field_x[f],67,field_names[f],7,1);
    double y=78;
    for(size_t i=first;i<end;++i) {
        p.line(24,y-6,o.events?255:456);
        for(int h=0;h<24;++h) {
            if(o.shade_rows && h%2) p.shade_row(24,y,o.events?255:456);
            auto s=days[i].body[NAV_SUN][h], m=days[i].body[NAV_MOON][h], n=days[i].body[NAV_MOON][h+1];
            if(h==0) p.text(24,y,std::to_string(days[i].day),7,1);
            p.text(36,y,hour(h),7);
            std::string fields[]={"",
                angle(s.gha_deg),angle(s.dec_deg,true),angle(m.gha_deg),angle(m.dec_deg,true),
                num(m.hp_deg*60),num((wrap(n.gha_deg-m.gha_deg)-14.-19./60)*60),num((n.dec_deg-m.dec_deg)*60)};
            for(int f=1;f<8;++f) p.text(field_x[f],y,fields[f],o.events?7.6:8.5);
            y+=7.5;
        }
    }
    y=632;
    for(size_t i=first;i<end;++i) {
        auto s=days[i].body[NAV_SUN][12], n=days[i].body[NAV_SUN][13], m=days[i].body[NAV_MOON][12];
        p.text(24,y,"Day "+std::to_string(days[i].day)+" noon Sun: v "+num((wrap(n.gha_deg-s.gha_deg)-15)*60)+" d "+
            num((n.dec_deg-s.dec_deg)*60)+" SD "+num(s.sd_deg*60)+" | Moon SD "+num(m.sd_deg*60),o.events?7:9);
        y+=12;
    }
    if(o.events) events(p,days,first,end,true);
}

static void increments(PDF &p, bool shaded) {
    // One page per elapsed minute keeps GHA, v/d and seconds in one lookup.
    // Both correction components print to the requested tenth of an arcminute.
    for(int minute=0;minute<60;++minute) {
        p.start("INCREMENTS AND CORRECTIONS | Minute "+hour(minute),
                minute==0?"Increments and corrections (minutes/seconds)":"");
        p.text(24,58,"GHA INCREMENT (deg min)",8,1);
        p.text(180,58,"v/d RATE '/h  |  WHOLE-MINUTE CORRECTION '",8,1);
        p.text(24,76,"sec",8,1); p.fit_text(48,76,"Sun/planets",38,7.5,1);
        p.text(89,76,"Aries",8,1); p.text(130,76,"Moon",8,1);
        p.line(24,82,144);
        if(shaded) p.striped_rows(24,92,144,7.8,60,2);
        for(int second=0;second<60;++second) {
            double y=92+second*7.8;
            p.text(24,y,hour(second),8.5);
            // Rates in millionths of a tenth of an arcminute/hour. Integer
            // half-up rounding also makes exact .05' ties deterministic.
            int col=0;for(long long rate:{9000000000LL,9024641184LL,8590000000LL}) {
                auto ticks=((minute*60+second)*rate+1800000000LL)/3600000000LL;
                p.text(48+41*col++,y,angle(ticks/600.),8.5);
            }
            if(second%10==9) p.line(24,y+1,144,.15);
        }
        for(int col=0;col<6;++col) {
            double x=180+col*50;
            p.right(x+18,76,"Rate",7.5,1); p.right(x+46,76,"Corr",7.5,1);
            p.line(x,82,46);
            if(shaded) p.striped_rows(x,92,46,9.2,51,2);
            for(int row=0;row<51;++row) {
                int tenths=col*51+row; double y=92+row*9.2;
                p.right(x+18,y,num(tenths/10.),8.5);
                // Exact positive half-up rounding to tenths of an arcminute.
                p.right(x+46,y,num(((tenths*minute+30)/60)/10.),8.5);
                if(row%10==9) p.line(x,y+2,46,.15);
            }
        }
        p.text(24,575,"USING THIS MINUTE PAGE",8.5,1);
        double y=590;
        p.paragraph(24,y,"Add the GHA increment at the seconds row to the whole-hour GHA. "
            "For v or d, read the rate magnitude above and add the seconds correction at right. "
            "Interpolate that small table in rate and seconds. Round the sum to 0.1', "
            "then apply the original v/d sign. For declination omit the GHA increment.",220,8.5,10);
        p.text(24,654,"Check: 34m56s, v +2.4'/h: 1.4' + 0.0' = +1.4'.",8);
        p.text(24,667,"GHA 123 45.6 + 008 44.0 + 1.4' = 132 31.0.",8);
        p.text(258,575,"SECONDS CORRECTION (arcminutes)",8.5,1);
        p.text(258,587,"Add to whole-minute correction; use same rate.",7.5);
        p.right(278,601,"Rate",7.5,1);
        int j=0;for(int sec:{0,10,20,30,40,50,60}) p.right(304+j++*29,601,std::to_string(sec)+"s",7.5,1);
        p.line(258,605,220);
        if(shaded) p.striped_rows(258,617,220,9,7,2);
        int row=0;for(int tenths:{0,50,100,150,200,250,305}) {
            double yy=617+row++*9;
            p.right(278,yy,num(tenths/10.),8.5);
            j=0;for(int sec:{0,10,20,30,40,50,60})
                p.right(304+j++*29,yy,num(((tenths*sec+1800)/3600)/10.),8.5);
        }
        p.text(24,684,"Corrections in 0.1'; rounded components can change the last digit. Reduce final GHA modulo 360.",8);
    }
}
static double refraction(double ha) { return (1/std::tan((ha+7.31/(ha+4.4))*pi/180)); }
static void corrections(PDF &p, bool shaded) {
    p.start("ALTITUDE CORRECTIONS | COMPONENT TABLES","Altitude corrections");
    double y=63;
    p.paragraph(24,y,"Hs + signed index correction - dip = apparent altitude Ha. Subtract refraction; "
        "add parallax and signed semidiameter (+ lower limb, - upper limb). Artificial horizon: "
        "apply index correction, halve the angle and omit dip. These are components, not the "
        "official combined-limb tables.",0,9.5,12);
    p.text(24,125,"DIP (subtract)",9,1);
    p.text(180,125,"REFRACTION (subtract)",8.5,1);
    p.text(318,125,"PARALLAX (add, arcminutes)",8,1);
    for(auto h:std::vector<std::pair<double,std::string>>{{46,"Eye m"},{78,"Dip '"},{115,"Eye ft"},{152,"Dip '"},
        {208,"Ha deg"},{267,"Refr '"},{337,"Ha"},{369,"HP 54'"},{401,"57'"},{433,"60'"},{465,"63'"}})
        p.right(h.first,140,h.second,8,1);
    p.line(24,145,130);p.line(180,145,92);p.line(318,145,158);
    if(shaded) {
        p.striped_rows(24,159,130,9.7,40,3);
        p.striped_rows(180,159,92,9.7,41,3);
        p.striped_rows(318,159,158,22,18,6);
    }
    for(int i=0;i<41;++i) {
        double yy=159+i*9.7;
        if(i<40) {
            double m=(i+1)*.5,ft=(i+1)*2.;
            p.right(46,yy,num(m),9);p.right(78,yy,num(1.76*std::sqrt(m)),9);
            p.right(115,yy,num(ft,0),9);p.right(152,yy,num(.97*std::sqrt(ft)),9);
        }
        double h=i<10?5+i*.5:10+(i-10)*2.5;
        p.right(208,yy,num(h),9);p.right(267,yy,num(refraction(h)),9);
        if(i%5==4) {p.line(24,yy+3,130,.15);p.line(180,yy+3,92,.15);}
    }
    for(int h=5;h<=90;h+=5) {
        double yy=159+(h/5-1)*22;
        p.right(337,yy,num(h,0),9);
        int j=0;for(int hp:{54,57,60,63}) p.right(369+j++*32,yy,num(asn(sn(hp/60.)*cs(h))*60),9);
        if(h%15==0) p.line(318,yy+4,158,.15);
    }
    y=579;
    p.paragraph(24,y,"Dip: 1.76 sqrt(eye height in m), or 0.97 sqrt(height in ft), in arcminutes. "
        "Refraction: Bennett apparent-altitude approximation, 1010 mb and 10 C. Scale refraction "
        "by (pressure / 1010) * (283 / (273 + temperature C)). Below Ha 5 degrees use official tables.",0,9,12);
    p.paragraph(24,y,"Parallax = asin(sin HP * cos Ha). HP and SD are on the daily pages in arcminutes. "
        "For Moon, topocentric SD augmentation, limb geometry and correction order also matter. "
        "These components do not replace the official Moon limb tables; NAVALM's sight reducer "
        "provides worked software corrections.",0,9,12);
}
static void formulas(PDF &p) {
    p.start("CONCISE CALCULATOR SIGHT REDUCTION","Calculator reduction guide");
    double y=70;
    p.paragraph(24,y,"These direct spherical-triangle formulas work from your DR or assumed position. "
        "This is a NAVALM calculator guide, not the NAO Concise lookup tables. Use degree mode. "
        "Latitude and declination are positive north, negative south. Longitude is positive east.");
    for(auto s:{"LHA = (GHA + longitude) modulo 360", "up = sin(lat)*sin(dec) + cos(lat)*cos(dec)*cos(LHA)",
        "north = cos(lat)*sin(dec) - sin(lat)*cos(dec)*cos(LHA)",
        "east = -cos(dec)*sin(LHA)","Hc = atan2(up, sqrt(north*north + east*east))",
        "Zn = atan2(east, north), reduced to 0 through 360", "Intercept NM = (Ho - Hc) * 60"}) {
        p.text(24,y,s,9,2); y+=26;
    }
    p.paragraph(24,y,"atan2 conventions differ between calculators: verify that atan2(east, north) "
        "means the angle whose sine follows east and cosine follows north. Positive intercept is "
        "Toward the body's geographic position, negative is Away. A body below the horizon has "
        "negative Hc. At the zenith or nadir, azimuth is undefined.");
    p.text(24,y+10,"WORKSHEET | signed north/east positive",10,1);
    int row=0;
    for(auto field:{"Lat _______ N/S  signed _______   Lon _______ E/W  signed _______",
        "GHA _______ Dec _______ N/S  signed _______   LHA _______",
        "up ______________  north ______________  east ______________",
        "Hc ______________  Zn (0-360) ______________",
        "Ho _______  Ho-Hc _______ x60 = _______ NM   Toward / Away",
        "AUDIT: degree mode [ ]  longitude sign [ ]  azimuth quadrant [ ]"})
        p.text(24,y+36+row++*30,field,9,2);
}
static void ageton(PDF &p, bool shaded) {
    Ageton table;
    p.start("COMPACT AGETON | INSTRUCTIONS","Compact Ageton (25 pages)");
    double y=65;
    p.paragraph(24,y,"A(x) = -1,000,000 log10(abs(sin x)); B(x) = -1,000,000 log10(abs(cos x)). "
        "This edition uses a half-minute grid and SIX-place logarithms, a tenfold scale change "
        "from traditional Ageton tables. All entries are freshly calculated rounded integers. "
        "Never combine them with historical 100,000-scale entries.");
    p.paragraph(24,y,"Each row lists x, A(x), B(x), and c = 90 - x. To use c, SWAP the A and B "
        "columns: A(c)=B(x), B(c)=A(x). Thus 0 through 45 degrees covers 0 through 90. "
        "For obtuse or signed angles, use absolute sine/cosine magnitudes and carry signs "
        "separately as below. INF is a singular value, not a number for subtraction.");
    p.paragraph(24,y,"Let t be the smaller of LHA and 360-LHA (0 to 180). Let q0 be an acute "
        "auxiliary angle. The Q used below is a signed meridian ANGLE that may exceed 90 degrees; "
        "it is not a signed latitude. This convention keeps the far-side case explicit.");
    for(auto s:{"1  A(R) = A(t) + B(dec)          -> R; look up B(R)",
        "2  A(q0) = A(dec) - B(R)", "   B(q0) = B(dec) + B(t) - B(R)",
        "   A(q0) >=150515: invert A; otherwise invert B.",
        "3  t <90: Q = sign(dec)*q0", "   t >90: Q = sign(dec)*(180-q0)",
        "4  gap = Q - lat, normalized to -180 through +180",
        "5  A(abs Hc) = B(R) + B(gap)     -> abs Hc",
        "6  A(z0) = A(R) - B(Hc)",
        "   B(z0) = B(R) + A(gap) - B(Hc)",
        "   A(z0) >=150515: invert A; otherwise invert B.",
        "7  gap >=0: Z = z0; gap <0: Z = 180-z0",
        "8  LHA <180: Zn = 360-Z; LHA >180: Zn = Z"}) {
        p.text(24,y,s,8.4,2); y+=20;
    }
    p.paragraph(24,y,"Choose the nearest printed row for a quick estimate. For finer work use "
        "linear interpolation in forward and inverse lookups. Half-minute spacing does not "
        "guarantee a half-minute final altitude: rounding can be amplified by the triangle.");
    y+=8;
    p.text(24,y,"INTERPOLATION AND COMPLEMENTS",10,1); y+=18;
    p.paragraph(24,y,"Example: x = 30 degrees 00.2 minutes lies 0.4 of the way from the 00.0 "
        "to the 00.5 row. Interpolate the rounded A or B entries by that fraction. For an inverse "
        "lookup, divide the log difference by the difference between adjacent printed entries "
        "to obtain the same fraction of a half minute.",0,10,13);
    p.paragraph(24,y,"For an angle above 45 degrees, find it in the c column and swap A/B. "
        "For example, at c=60 degrees use the x=30 degrees row: A(60)=B(30), B(60)=A(30). "
        "Always record whether you used x or c before copying a log.",0,10,13);
    p.start("COMPACT AGETON | LIMITS AND WORKSHEET");
    y=65;
    p.paragraph(24,y,"Stop and use direct formulas for t at 0, 90 or 180 degrees, declination "
        "within half a minute of zero, R above 87 degrees, or Hc outside 5 to 85 degrees. "
        "Do not subtract INF from INF or conceal a negative logarithm. A rounded lookup at "
        "a singular endpoint is a warning to change method, not evidence of an exact answer.");
    p.paragraph(24,y,"If abs(gap) exceeds 90 degrees the body is below the horizon. Hc is then "
        "negative; do not treat the positive acute lookup as an observable altitude. At abs(gap) "
        "=90, Hc=0. The printed worksheet deliberately asks you to check these signs before plotting.");
    for(auto s:{"Lat ______ N/S  signed ______   Lon ______ E/W  signed ______",
        "GHA ______  Dec ______ N/S  signed ______  Ho ______",
        "LHA (0-360) ______  t ______  [ ] t <90  [ ] t >90",
        "A(t) ______  B(dec) ______  A(R) ______  R ______  B(R) ______",
        "A(dec) ______  A(q0) ______  B(q0) ______  q0 ______",
        "Q sign from dec ____  Q magnitude: q0 / 180-q0 ____",
        "SIGNED Q ______  SIGNED lat ______  SIGNED gap ______",
        "[ ] abs gap <90: above  [ ] >90: below - STOP",
        "B(gap) ______  A(abs Hc) ______  Hc ______  B(Hc) ______",
        "A(z0) ______  B(z0) ______  z0 ______  gap sign ____",
        "[ ] gap >=0: Z=z0  [ ] gap <0: Z=180-z0   Z ______",
        "[ ] LHA <180: 360-Z  [ ] LHA >180: Z   Zn ______",
        "Ho-Hc ______ x60 = ______ NM   [ ] Toward  [ ] Away",
        "AUDIT: [ ] six-place scale  [ ] complement columns swapped",
        "[ ] interpolation recorded  [ ] limits checked  [ ] signs checked"}) {
        p.text(24,y,s,8.5,2); y+=28;
    }
    // Four half-degree blocks per page. 90 blocks cover x=0..45, with
    // complementary angles supplying 45..90. Exactly 23 table pages.
    for(int first=0;first<90;first+=4) {
        p.start("COMPACT AGETON | "+angle(first*.5)+" to "+angle(std::min(first+4,90)*.5));
        for(int c=0;c<4 && first+c<90;++c) {
            int block=first+c; double x=24+c*115;
            p.text(x+8,59,"x",8,1); p.right(x+52,59,"A(x)",8,1);
            p.right(x+80,59,"B(x)",8,1); p.text(x+92,59,"c",8,1);
            p.line(x,66,110);
            if(shaded) p.striped_rows(x,79,110,9.2,61,3);
            for(int row=0;row<=60;++row) {
                double v=block*.5+row*Ageton::step;
                double av=table.a(v), bv=table.b(v);
                auto logstr=[](double n) { return std::isfinite(n)?num(n,0):"INF"; };
                auto smallangle=[](double n) { char label[24]; int ticks=int(std::lround(n*120));
                    std::snprintf(label,sizeof label,"%02d %04.1f",ticks/120,(ticks%120)/2.); return std::string(label); };
                double baseline=79+row*9.2;
                p.text(x,baseline,smallangle(v),7.5);
                p.right(x+52,baseline,logstr(av),7.5);
                p.right(x+80,baseline,logstr(bv),7.5);
                p.text(x+83,baseline,smallangle(90-v),7.5);
                if(row%10==0) p.line(x,82+row*9.2,110,.15);
            }
        }
        if(first==88) {
            double y=70;
            p.text(261,y,"WORKED CHECK AND TABLE AUDIT",9,1); y+=20;
            p.paragraph(261,y,"Nearest-row check: latitude 20 S, declination 15 N. LHA 60 and 300 "
                "exercise opposite east/west branches with the same altitude.",219,9,12);
            for(double lha:{60.,300.}) {
                auto r=table.reduce(-20,15,lha);
                p.text(261,y,"LHA "+num(lha,0)+": R "+angle(r.r)+"  Q "+angle(r.q,true),8); y+=15;
                p.text(261,y,"Hc "+angle(r.altitude)+"  Zn "+angle(r.azimuth),8); y+=24;
            }
            p.paragraph(261,y,"NUMERICAL CHECK: 200,000 random cases per domain, solar-range "
                "and full-sky. Within the stated limits, sampled maximum Hc errors were "
                "0.95 minute using nearest rows and 0.26 minute using interpolation. "
                "These are sample results, not universal error bounds.",219,9,12);
            p.paragraph(261,y,"HAND CHECK: try all four latitude/declination hemisphere pairs, "
                "both LHA halves and both t quadrants. Record the sign of Q, latitude and gap. "
                "Confirm the azimuth quadrant before plotting.",219,9,12);
            p.paragraph(261,y,"BACKGROUND: Ageton's divided navigational triangle (H.O. 211). "
                "NAVALM independently generates these entries and its worksheet. "
                "www.siranah.de/html/sail040u.htm describes the underlying functions.",219,9,12);
            p.paragraph(261,y,"This edition's signed meridian angle Q and 1,000,000 log scale "
                "differ from other Ageton variants. Follow these instructions and tables together. "
                "Do not mix procedures or log scales.",219,9,12);
        }
        p.text(24,663,"x,c = DD MM.m | c=90-x: SWAP A/B | scale 1,000,000 | INF: stop",8);
        p.text(24,677,"Near singular angles, change method. Half-minute rows do not guarantee half-minute Hc.",7);
    }
}
std::string generate(const Options &o, const std::function<bool(int,const std::string&)> &progress) {
    if(o.year<1600 || o.year>2200) throw std::runtime_error("Choose a Gregorian year from 1600 to 2200.");
    if(!(o.sun_moon||o.planets||o.stars||o.events||o.increments||o.corrections||o.reduction!=Reduction::None))
        throw std::runtime_error("Select at least one book section.");
    auto days=calendar(o.year);
    bool astronomy=o.sun_moon||o.planets||o.stars||o.events;
    if(astronomy) {
        char err[512]; if(nav_engine_init(err,sizeof err)) throw std::runtime_error(err);
        if(!nav_engine_covers(days.front().jd) || !nav_engine_covers(days.back().jd+1))
            throw std::runtime_error("The selected ephemeris must cover the whole year and next January 1.");
    }
    PDF p(o.format); opening(p,o);
    if(o.corrections) corrections(p,o.shade_rows);
    for(size_t first=0;astronomy && first<days.size();first+=3) {
        if(progress && !progress(int(first*90/days.size()),date(days[first])))
            throw std::runtime_error("Book generation cancelled.");
        size_t end=std::min(first+3,days.size());
        for(size_t i=first;i<end;++i) collect(days[i],o);
        if(o.planets||o.stars) planets_page(p,days,first,end,o);
        if(o.sun_moon) sun_moon_page(p,days,first,end,o);
        if(o.events && !o.sun_moon) events(p,days,first,end,false);
    }
    if(progress && !progress(92,"Reference sections")) throw std::runtime_error("Book generation cancelled.");
    if(o.reduction==Reduction::Formulas || o.reduction==Reduction::Both) formulas(p);
    if(o.reduction==Reduction::Ageton || o.reduction==Reduction::Both) ageton(p,o.shade_rows);
    if(o.increments) increments(p,o.shade_rows);
    if(progress && !progress(100,"Complete")) throw std::runtime_error("Book generation cancelled.");
    return p.document();
}
void save(const std::string &path, const std::string &pdf) {
    auto temporary=std::filesystem::u8path(path+".part-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        {
            std::ofstream out(temporary,std::ios::binary);
            if(!out || !out.write(pdf.data(),std::streamsize(pdf.size())) || !out.flush())
                throw std::runtime_error("Cannot write annual book: "+path);
        }
#ifdef _WIN32
        bool failed=!MoveFileExW(temporary.c_str(),std::filesystem::u8path(path).c_str(),
                               MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
        bool failed=std::rename(temporary.c_str(),path.c_str())!=0;
#endif
        if(failed)
            throw std::runtime_error("Cannot install annual book: "+path);
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
