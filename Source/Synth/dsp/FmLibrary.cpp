#include "Synth/dsp/FmLibrary.h"
#include "Synth/dsp/Presets.h"
#include <algorithm>

namespace el::chimera {

namespace {
    int cartsIn (const fmlib::Category& c) { return (c.count + 31) / 32; }
    bool validCat (int c) { return c >= 0 && c <= fmlib::kCategoryCount; }
}

int fmLibCategoryCount() { return fmlib::kCategoryCount + 1; }

const char* fmLibCategoryName (int c)
{
    if (! validCat (c)) return "";
    return c == 0 ? "Chimera" : fmlib::kCategories[c - 1].name;
}

int fmLibCategoryCarts (int c)
{
    if (! validCat (c)) return 0;
    return c == 0 ? 1 : cartsIn (fmlib::kCategories[c - 1]);
}

int fmLibCategoryFirstCart (int c)
{
    if (! validCat (c)) return 0;
    int first = 0;
    for (int i = 0; i < c; ++i) first += fmLibCategoryCarts (i);
    return first;
}

int fmLibCartCount() { return fmLibCategoryFirstCart (fmlib::kCategoryCount) + fmLibCategoryCarts (fmlib::kCategoryCount); }
int fmLibVoiceCount() { return (int) chimeraCart().size() + fmlib::kVoiceCount; }

int fmLibCartCategory (int cart)
{
    if (cart < 0) return 0;
    for (int c = 0; c < fmLibCategoryCount(); ++c) {
        cart -= fmLibCategoryCarts (c);
        if (cart < 0) return c;
    }
    return fmLibCategoryCount() - 1;
}

std::string fmLibCartName (int cart)
{
    if (cart <= 0 || cart >= fmLibCartCount()) return kChimeraCartName;
    const int c = fmLibCartCategory (cart);
    return std::string (fmLibCategoryName (c)) + " " + std::to_string (cart - fmLibCategoryFirstCart (c) + 1);
}

int fmLibFindCart (const std::string& name)
{
    if (name == kChimeraCartName) return 0;
    const auto sp = name.rfind (' ');
    if (sp == std::string::npos || sp + 1 >= name.size()) return -1;
    int n = 0;
    for (size_t i = sp + 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9' || n > 100000) return -1;
        n = n * 10 + (name[i] - '0');
    }
    const auto cat = name.substr (0, sp);
    for (int c = 1; c < fmLibCategoryCount(); ++c)
        if (cat == fmLibCategoryName (c))
            return n >= 1 && n <= fmLibCategoryCarts (c) ? fmLibCategoryFirstCart (c) + n - 1 : -1;
    return -1;
}

std::vector<Dx7Patch> fmLibCart (int cart)
{
    if (cart <= 0 || cart >= fmLibCartCount()) return chimeraCart();
    const int c = fmLibCartCategory (cart);
    const auto& k = fmlib::kCategories[c - 1];
    const int first = (cart - fmLibCategoryFirstCart (c)) * 32;
    const int n = std::min (32, k.count - first);
    std::vector<Dx7Patch> out;
    out.reserve ((size_t) n);
    for (int i = 0; i < n; ++i) out.push_back (dx7FromPacked (fmlib::kVoices + (size_t) (k.firstVoice + first + i) * 128));
    return out;
}

} // namespace el::chimera
