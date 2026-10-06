// The window's own buttons, inside the toolbar.
//
// The v4 design puts the three macOS window buttons at x = 25 / 45 / 65, at the top of the
// sidebar card and level with DINE's one toolbar, not on a title bar above it (docs/design/v4). That is a window-server
// arrangement, not a drawing one: macOS still draws and runs those buttons, and the window is
// still a native, resizable, full-screen-capable window - the content view is simply allowed
// to extend under a title bar that has been made transparent and emptied of its title.
//
// JUCE has no call for this, so this is the one place the application speaks to AppKit about
// its own window. Everything here is a no-op on any window it is not given, and DINE looks
// and works the same if it silently does nothing: the toolbar leaves the room at its left
// empty either way.
//
// A transparent title bar is still only 28 pt tall, so macOS centres its buttons on 14 pt
// while everything else in the toolbar is centred on 26. The buttons are moved down onto the
// toolbar's centre line, the way a Mac application with a unified toolbar has them, and moved
// again whenever AppKit lays the title bar out afresh (a resize, leaving full screen, a
// change of key window).
//
// The toolbar is JUCE's view, not the title bar, so AppKit never sees a click on it: dragging
// the window by the toolbar and double-clicking it to zoom are asked for here, by MainView,
// through the two calls at the bottom.

#include <juce_gui_basics/juce_gui_basics.h>

#import <AppKit/AppKit.h>

namespace livemix
{

namespace
{
    NSWindow* nativeWindow (juce::Component& c)
    {
        auto* top = c.getTopLevelComponent();
        auto* handle = top != nullptr ? top->getWindowHandle() : nullptr;
        if (handle == nullptr) return nil;
        return [(NSView*) handle window];
    }

    // v4: the buttons sit at the top of the sidebar card, 25 / 45 / 65 across and centred on
    // y = 32, and stay there when the sidebar is hidden. The container is 64 tall so that its
    // centre line is 32; the toolbar itself is 60 (Dine::Metric::toolbar).
    constexpr CGFloat kToolbarHeight = 64.0;
    constexpr CGFloat kFirstButtonX  = 25.0;
    constexpr CGFloat kButtonPitch   = 20.0;

    void placeWindowButtons (NSWindow* w)
    {
        if (w == nil || (w.styleMask & NSWindowStyleMaskFullScreen) != 0) return;

        NSButton* close = [w standardWindowButton: NSWindowCloseButton];
        NSView* titlebar = close.superview;
        NSView* container = titlebar.superview;
        if (close == nil || titlebar == nil || container == nil) return;

        // The title bar's container grows to the toolbar's height, pinned to the top of the window.
        NSRect frame = container.frame;
        frame.size.height = kToolbarHeight;
        frame.origin.y = w.frame.size.height - kToolbarHeight;
        container.frame = frame;
        titlebar.frame = container.bounds;

        const NSWindowButton kinds[] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
        for (int i = 0; i < 3; ++i)
        {
            NSButton* b = [w standardWindowButton: kinds[i]];
            if (b == nil) continue;
            const CGFloat y = std::round ((titlebar.bounds.size.height - b.frame.size.height) * 0.5);
            [b setFrameOrigin: NSMakePoint (kFirstButtonX + kButtonPitch * i, y)];
        }
    }
}

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

    placeWindowButtons (w);

    // AppKit puts the buttons back wherever it lays the title bar out again. (No ARC here: the
    // window is held by JUCE, not by these blocks, and they go when it closes.)
    __block NSWindow* target = w;
    auto* centre = [NSNotificationCenter defaultCenter];
    NSMutableArray* tokens = [[NSMutableArray alloc] init];
    for (NSNotificationName name : { NSWindowDidResizeNotification, NSWindowDidEndLiveResizeNotification,
                                     NSWindowDidExitFullScreenNotification, NSWindowDidBecomeKeyNotification,
                                     NSWindowDidResignKeyNotification, NSWindowDidChangeBackingPropertiesNotification })
    {
        [tokens addObject: [centre addObserverForName: name object: w queue: nil usingBlock: ^(NSNotification*) {
            placeWindowButtons (target);
            // Some of these land before AppKit's own layout pass; once more after it.
            dispatch_async (dispatch_get_main_queue(), ^{ if (target != nil) placeWindowButtons (target); });
        }]];
    }
    __block id closing = nil;
    closing = [centre addObserverForName: NSWindowWillCloseNotification object: w queue: nil usingBlock: ^(NSNotification*) {
        for (id t in tokens) [centre removeObserver: t];
        [tokens release];
        [centre removeObserver: closing];
        target = nil;
    }];
}

// A press on the empty toolbar moves the window, the way a press on a title bar does -
// with the window server's own drag, so it snaps and tiles like any other window.
void dragWindowFromToolbar (juce::Component& c)
{
    NSWindow* w = nativeWindow (c);
    NSEvent* e = [NSApp currentEvent];
    if (w == nil || e == nil || e.type != NSEventTypeLeftMouseDown) return;
    [w performWindowDragWithEvent: e];
}

// A double-click on the empty toolbar does what System Settings > Desktop & Dock says a
// double-click on a title bar does: zoom (the default), minimise, or nothing.
void toolbarDoubleClicked (juce::Component& c)
{
    NSWindow* w = nativeWindow (c);
    if (w == nil || (w.styleMask & NSWindowStyleMaskFullScreen) != 0) return;

    NSString* action = [[NSUserDefaults standardUserDefaults] stringForKey: @"AppleActionOnDoubleClick"];
    if (action == nil)
        action = [[NSUserDefaults standardUserDefaults] boolForKey: @"AppleMiniaturizeOnDoubleClick"] ? @"Minimize" : @"Maximize";

    if ([action isEqualToString: @"None"]) return;
    if ([action isEqualToString: @"Minimize"]) [w performMiniaturize: nil];
    else                                       [w performZoom: nil];
}



// Accessibility > Display > Reduce motion. The sidebar's slide is instant when it is on.
bool systemPrefersReducedMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

} // namespace livemix
