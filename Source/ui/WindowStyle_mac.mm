// CONTRXL 25 - macOS native window styling: always use the dark title bar,
// whatever the system appearance (Light / Dark / Auto) is set to.
#import <Cocoa/Cocoa.h>
#include "WindowStyle.h"

namespace cx::ui
{
void makeTitleBarDark (juce::Component& window)
{
    auto* peer = window.getPeer();
    if (peer == nullptr) return;
    auto* view = (NSView*) peer->getNativeHandle();
    if (view == nil) return;
    NSWindow* w = [view window];
    if (w == nil) return;
    if (@available (macOS 10.14, *))
        [w setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua]];
    [w setBackgroundColor: [NSColor blackColor]];
}
}
