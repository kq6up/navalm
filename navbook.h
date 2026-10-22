/* SPDX-License-Identifier: MIT */
#ifndef NAVBOOK_H
#define NAVBOOK_H
#include <functional>
#include <string>
#include <vector>

namespace navbook {
enum class Reduction { None, Formulas, Ageton, Both };
struct Options {
    int year = 2027;
    bool sun_moon = true, planets = true, stars = true;
    bool events = true, increments = true, corrections = true;
    Reduction reduction = Reduction::Both;
};
// Call from one thread only: the shared astronomy engine is not reentrant.
// The callback returns false to cancel. No destination file is touched here.
std::string generate(const Options &, const std::function<bool(int, const std::string &)> & = {});
void save(const std::string &path, const std::string &pdf);

// Values used by the printed tables and by the independent numerical audit.
class Ageton {
    std::vector<double> a_;
  public:
    static constexpr double step = 0.5 / 60.;
    static constexpr double scale = 1000000.;
    Ageton();
    double a(double degrees, bool interpolate = false) const;
    double b(double degrees, bool interpolate = false) const;
    double inverse_a(double value, bool interpolate = false) const;
    double inverse_b(double value, bool interpolate = false) const;
    struct Result {
        bool usable = false;
        double altitude = 0, azimuth = 0, r = 0, q = 0;
        std::string reason;
    };
    Result reduce(double latitude, double declination, double lha,
                  bool interpolate = false) const;
};
}
#endif
