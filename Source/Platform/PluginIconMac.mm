#include "PluginIconMac.h"
#include <JuceHeader.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AppKit/AppKit.h>

namespace el {

namespace {
    OSType fourCharCode (juce::String s)
    {
        if (s.trim().length() >= 4)
            s = s.trim();
        s += "    ";
        return (((OSType) (unsigned char) s[0]) << 24)
             | (((OSType) (unsigned char) s[1]) << 16)
             | (((OSType) (unsigned char) s[2]) << 8)
             |  ((OSType) (unsigned char) s[3]);
    }

    // Same parsing as JUCE's AudioUnitFormatHelpers::getComponentDescFromIdentifier.
    bool parseIdentifier (const juce::String& id, AudioComponentDescription& desc)
    {
        if (! id.startsWithIgnoreCase ("AudioUnit:"))
            return false;
        juce::String s (id.substring (juce::jmax (id.lastIndexOfChar (':'), id.lastIndexOfChar ('/')) + 1));
        juce::StringArray tokens;
        tokens.addTokens (s, ",", juce::StringRef());
        tokens.removeEmptyStrings();
        if (tokens.size() != 3)
            return false;
        desc = {};
        desc.componentType         = fourCharCode (tokens[0]);
        desc.componentSubType      = fourCharCode (tokens[1]);
        desc.componentManufacturer = fourCharCode (tokens[2]);
        return true;
    }
}

juce::Image loadAudioUnitIcon (const juce::PluginDescription& description)
{
    if (description.pluginFormatName != "AudioUnit")
        return {};

    juce::Image result;

    @autoreleasepool
    {
        AudioComponentDescription cd {};
        if (! parseIdentifier (description.fileOrIdentifier, cd))
            return {};

        AudioComponent comp = AudioComponentFindNext (nullptr, &cd);
        if (comp == nullptr)
            return {};

        NSImage* img = nil;
        if (@available (macOS 11.0, *))
            img = AudioComponentGetIcon (comp);   // macOS signature (the 2-argument form is iOS-only); nil if no icon
        if (img == nil)
            return {};

        NSRect rect = NSMakeRect (0, 0, [img size].width, [img size].height);
        CGImageRef cg = [img CGImageForProposedRect: &rect context: nil hints: nil];
        if (cg == nullptr)
            return {};

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage: cg];
        NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
        const bool havePng = png != nil && [png length] > 0;
        if (havePng)
            result = juce::ImageFileFormat::loadFrom ([png bytes], (size_t) [png length]);
#if ! __has_feature(objc_arc)
        [rep release];
#endif
    }

    return result;
}

} // namespace el
