#include "MacFonts.h"

#ifdef __APPLE__
#include <CoreText/CoreText.h>  // for CTFontManagerRegisterFontsForURL
#endif

namespace ambitus::macfonts {

bool registerFont(const std::string& file) {
#ifdef __APPLE__
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(file.c_str()),
                                                           static_cast<CFIndex>(file.size()), false);
    if (!url) {
        return false;
    }
    CFErrorRef error = nullptr;
    const bool ok = CTFontManagerRegisterFontsForURL(url, kCTFontManagerScopeProcess, &error);
    if (error) {
        CFRelease(error);
    }
    CFRelease(url);
    return ok;
#else
    (void)file;
    return false;
#endif
}

}  // namespace ambitus::macfonts
