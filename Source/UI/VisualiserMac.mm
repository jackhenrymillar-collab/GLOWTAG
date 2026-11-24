#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#import <Cocoa/Cocoa.h>

// The two things the visualiser needs from macOS itself when its picture sits behind the app:
// keep the picture's window directly under the app's window (it then moves, hides and goes
// full screen with it), and let the pointer pass straight through it.
namespace VisNative
{
    void placeBehind (juce::Component& picture, juce::Component& appWindow);
    void hide (juce::Component& picture);
    void letGo (juce::Component& picture);
    void watch (juce::Component& appWindow, std::function<void (int)> changed);
    void unwatch();

    static NSWindow* windowOf (juce::Component& c)
    {
        auto* view = (NSView*) c.getWindowHandle();
        return view != nil ? [view window] : nil;
    }

    void placeBehind (juce::Component& picture, juce::Component& appWindow)
    {
        NSWindow* pic = windowOf (picture);
        NSWindow* app = windowOf (appWindow);
        if (pic == nil || app == nil) return;
        [pic setIgnoresMouseEvents: YES];
        [pic setHasShadow: NO];
        if ([pic parentWindow] != app)
            [app addChildWindow: pic ordered: NSWindowBelow];
        // A see-through window redraws its shadow on every change, which is slow: the picture's window is solid, so none is needed
        [app setHasShadow: NO];
    }

    // Takes the picture off the screen for a moment (while the app's window changes size)
    void hide (juce::Component& picture)
    {
        NSWindow* pic = windowOf (picture);
        if (pic == nil) return;
        if ([pic parentWindow] != nil) [[pic parentWindow] removeChildWindow: pic];
        [pic orderOut: nil];
    }

    void letGo (juce::Component& picture)
    {
        NSWindow* pic = windowOf (picture);
        if (pic != nil && [pic parentWindow] != nil)
            [[pic parentWindow] removeChildWindow: pic];
    }

    // Tells the visualiser when the app's window starts changing size (0), has finished (1),
    // or has simply been given a new size in one go (2)
    static NSMutableArray* watchers = nil;

    void watch (juce::Component& appWindow, std::function<void (int)> changed)
    {
        unwatch();
        NSWindow* app = windowOf (appWindow);
        if (app == nil) return;
        watchers = [[NSMutableArray alloc] init];
        auto add = [&] (NSNotificationName name, int what)
        {
            const std::function<void (int)> tell = changed;      // the block keeps its own copy
            const int event = what;
            id token = [[NSNotificationCenter defaultCenter] addObserverForName: name object: app queue: nil
                                                                    usingBlock: ^(NSNotification*) { tell (event); }];
            [watchers addObject: token];
        };
        add (NSWindowWillStartLiveResizeNotification, 0);
        add (NSWindowWillEnterFullScreenNotification, 0);
        add (NSWindowWillExitFullScreenNotification, 0);
        add (NSWindowDidEndLiveResizeNotification, 1);
        add (NSWindowDidEnterFullScreenNotification, 1);
        add (NSWindowDidExitFullScreenNotification, 1);
        add (NSWindowDidResizeNotification, 2);
    }

    void unwatch()
    {
        if (watchers == nil) return;
        for (id token in watchers) [[NSNotificationCenter defaultCenter] removeObserver: token];
        [watchers release];
        watchers = nil;
    }
}
