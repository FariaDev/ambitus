#include "LegacyRedrawable.h"

#include "model/Element.h"  // for Element
#include "util/Range.h"     // for Range

void LegacyRedrawable::rerenderRange(const Range& r) { rerenderRect(r.getX(), r.getY(), r.getWidth(), r.getHeight()); }

void LegacyRedrawable::rerenderElement(const Element* e) {
    const auto& rect = e->getBoundingBox();
    rerenderRect(rect.x, rect.y, rect.width, rect.height);
}
