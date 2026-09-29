// The window's own buttons, inside the toolbar.
//
// The v3 design puts the three macOS window buttons at x = 16 / 36 / 56 of DLIVE's one 52 pt
// toolbar, not on a title bar above it (`Toolbar v3.4`, 117:32462). That is a window-server
// arrangement, not a drawing one: macOS still draws and runs those buttons, and the window is
// still a native, resizable, full-screen-capable window - the content view is simply allowed
// to extend under a title bar that has been made transparent and emptied of its title.
//
// JUCE has no call for this, so this is the one place the application speaks to AppKit about
// its own window. Everything here is a no-op on any window it is not given, and DLIVE looks
// and works the same if it silently does nothing: the toolbar leaves the room at its left
// empty either way.
//
// The top of the toolbar stays the title bar as far as macOS is concerned, so dragging the
// window by it and double-clicking it to zoom keep working with no code of ours.

#include <juce_gui_basics/juce_gui_basics.h>

#import <AppKit/AppKit.h>

namespace livemix
{

void putWindowButtonsInTheToolbar (juce::Component& window)
{
    auto* handle = window.getWindowHandle();
    if (handle == nullptr) return;

    NSView* view = (NSView*) handle;
    NSWindow* w = [view window];
    if (w == nil) return;

    w.titlebarAppearsTransparent = YES;
    w.titleVisibility = NSWindowTitleHidden;
    w.styleMask |= NSWindowStyleMaskFullSizeContentView;
    // The toolbar draws its own plane, so the title bar must not draw one over it.
    [w setMovableByWindowBackground: NO];
}

} // namespace livemix
