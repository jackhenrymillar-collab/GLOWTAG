#include <juce_gui_basics/juce_gui_basics.h>
#import <Cocoa/Cocoa.h>

// The two things the visualiser needs from macOS itself when its picture sits behind the app:
// keep the picture's window directly under the app's window (it then moves, hides and goes
// full screen with it), and let the pointer pass straight through it.
namespace VisNative
{
    void placeBehind (juce::Component& picture, juce::Component& appWindow);
    void letGo (juce::Component& picture);

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
        [app addChildWindow: pic ordered: NSWindowBelow];
        // A see-through window redraws its shadow on every change, which is slow: the picture's window is solid, so none is needed
        [app setHasShadow: NO];
    }

    void letGo (juce::Component& picture)
    {
        NSWindow* pic = windowOf (picture);
        if (pic != nil && [pic parentWindow] != nil)
            [[pic parentWindow] removeChildWindow: pic];
    }
}
