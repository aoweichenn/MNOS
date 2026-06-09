#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <mnos/host/debugger_session.hpp>

namespace
{
constexpr std::string_view HOST_GUI_HELP_FLAG = "--help";
constexpr std::string_view HOST_GUI_SHORT_HELP_FLAG = "-h";
constexpr std::string_view HOST_GUI_SMOKE_FLAG = "--smoke";
constexpr std::string_view HOST_GUI_USAGE_TEXT = "usage: mnos_gui [--smoke]\n";
constexpr int HOST_GUI_SUCCESS_EXIT_CODE = 0;
constexpr int HOST_GUI_RUNTIME_ERROR_EXIT_CODE = 1;
constexpr int HOST_GUI_USAGE_ERROR_EXIT_CODE = 2;

constexpr CGFloat HOST_GUI_WINDOW_WIDTH = 1080.0;
constexpr CGFloat HOST_GUI_WINDOW_HEIGHT = 720.0;
constexpr CGFloat HOST_GUI_PANEL_PADDING = 12.0;
constexpr CGFloat HOST_GUI_STATUS_PANEL_WIDTH = 300.0;
constexpr CGFloat HOST_GUI_INPUT_HEIGHT = 30.0;
constexpr CGFloat HOST_GUI_BUTTON_WIDTH = 96.0;
constexpr CGFloat HOST_GUI_BUTTON_HEIGHT = 28.0;
constexpr CGFloat HOST_GUI_TOOLBAR_HEIGHT = 36.0;
constexpr CGFloat HOST_GUI_TOOLBAR_BUTTON_COUNT = 3.0;
constexpr CGFloat HOST_GUI_CONTROL_GAP = 8.0;
constexpr CGFloat HOST_GUI_TERMINAL_FONT_SIZE = 13.0;
constexpr CGFloat HOST_GUI_STATUS_FONT_SIZE = 12.0;
constexpr CGFloat HOST_GUI_MIN_WINDOW_WIDTH = 820.0;
constexpr CGFloat HOST_GUI_MIN_WINDOW_HEIGHT = 520.0;

[[nodiscard]] bool has_flag(const int argc, char** argv, const std::string_view flag) noexcept
{
    for (int argument_index = 1; argument_index < argc; ++argument_index)
    {
        if (std::string_view{argv[argument_index]} == flag)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_unknown_flag(const int argc, char** argv) noexcept
{
    for (int argument_index = 1; argument_index < argc; ++argument_index)
    {
        const std::string_view argument{argv[argument_index]};
        if (argument != HOST_GUI_HELP_FLAG &&
            argument != HOST_GUI_SHORT_HELP_FLAG &&
            argument != HOST_GUI_SMOKE_FLAG)
        {
            return true;
        }
    }
    return false;
}

void print_usage(std::ostream& output)
{
    output << HOST_GUI_USAGE_TEXT;
}

[[nodiscard]] NSString* ns_string_from_std(const std::string& text)
{
    NSString* const value =
        [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return value == nil ? @"" : value;
}

[[nodiscard]] std::string std_string_from_ns(NSString* value)
{
    const char* const utf8_text = [value UTF8String];
    return utf8_text == nullptr ? std::string{} : std::string{utf8_text};
}

[[nodiscard]] int run_smoke(std::ostream& output)
{
    mnos::host::HostDebuggerSession session;
    session.boot();
    static_cast<void>(session.submit_command_line("mem"));
    const mnos::host::HostDebuggerFrame frame = session.frame();
    output << frame.summary_text << '\n';
    return frame.snapshot.command_count == std::size_t{1} &&
            frame.display_text.find("memory_pages total=") != std::string::npos
        ? HOST_GUI_SUCCESS_EXIT_CODE
        : HOST_GUI_RUNTIME_ERROR_EXIT_CODE;
}

[[nodiscard]] NSButton* make_button(NSString* title, id target, SEL action, const NSRect frame)
{
    NSButton* const button = [[NSButton alloc] initWithFrame:frame];
    [button setTitle:title];
    [button setTarget:target];
    [button setAction:action];
    [button setBezelStyle:NSBezelStyleRounded];
    return button;
}

[[nodiscard]] NSScrollView* make_scroll_view(NSView* document_view, const NSRect frame)
{
    NSScrollView* const scroll_view = [[NSScrollView alloc] initWithFrame:frame];
    [scroll_view setDocumentView:document_view];
    [scroll_view setHasVerticalScroller:YES];
    [scroll_view setHasHorizontalScroller:YES];
    [scroll_view setAutohidesScrollers:NO];
    return scroll_view;
}

[[nodiscard]] NSTextView* make_text_view(NSFont* font, NSColor* text_color, NSColor* background_color)
{
    NSTextView* const text_view = [[NSTextView alloc] initWithFrame:NSZeroRect];
    [text_view setEditable:NO];
    [text_view setSelectable:YES];
    [text_view setFont:font];
    [text_view setTextColor:text_color];
    [text_view setBackgroundColor:background_color];
    [text_view setAutomaticQuoteSubstitutionEnabled:NO];
    [text_view setAutomaticDashSubstitutionEnabled:NO];
    [text_view setAutomaticTextReplacementEnabled:NO];
    return text_view;
}
}

@interface MnosGuiController : NSObject <NSApplicationDelegate, NSTextFieldDelegate>
@end

@implementation MnosGuiController
{
    NSWindow* window_;
    NSTextView* terminal_view_;
    NSTextView* status_view_;
    NSTextField* input_field_;
    std::unique_ptr<mnos::host::HostDebuggerSession> session_;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    session_ = std::make_unique<mnos::host::HostDebuggerSession>();
    [self buildWindow];
    [self bootMachine];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}

- (void)buildWindow
{
    const NSRect window_frame = NSMakeRect(
        0.0,
        0.0,
        HOST_GUI_WINDOW_WIDTH,
        HOST_GUI_WINDOW_HEIGHT);
    window_ = [[NSWindow alloc]
        initWithContentRect:window_frame
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window_ setTitle:@"MNOS Debugger"];
    [window_ setMinSize:NSMakeSize(HOST_GUI_MIN_WINDOW_WIDTH, HOST_GUI_MIN_WINDOW_HEIGHT)];
    [window_ center];

    NSView* const content_view = [window_ contentView];
    [content_view setWantsLayer:YES];
    [[content_view layer] setBackgroundColor:[[NSColor colorWithCalibratedWhite:0.08 alpha:1.0] CGColor]];

    const CGFloat content_width = NSWidth([content_view bounds]);
    const CGFloat content_height = NSHeight([content_view bounds]);
    const CGFloat right_panel_x = content_width - HOST_GUI_PANEL_PADDING - HOST_GUI_STATUS_PANEL_WIDTH;
    const CGFloat input_y = HOST_GUI_PANEL_PADDING;
    const CGFloat terminal_y = input_y + HOST_GUI_INPUT_HEIGHT + HOST_GUI_CONTROL_GAP;
    const CGFloat terminal_width = right_panel_x - (HOST_GUI_PANEL_PADDING * 2.0);
    const CGFloat terminal_height =
        content_height - terminal_y - HOST_GUI_TOOLBAR_HEIGHT - (HOST_GUI_PANEL_PADDING * 2.0);
    const CGFloat status_height = terminal_height + HOST_GUI_INPUT_HEIGHT + HOST_GUI_CONTROL_GAP;
    const CGFloat toolbar_y = content_height - HOST_GUI_PANEL_PADDING - HOST_GUI_BUTTON_HEIGHT;
    const CGFloat send_button_x = right_panel_x - HOST_GUI_CONTROL_GAP - HOST_GUI_BUTTON_WIDTH;
    const CGFloat input_width = send_button_x - HOST_GUI_PANEL_PADDING - HOST_GUI_CONTROL_GAP;
    const CGFloat toolbar_button_width =
        (HOST_GUI_STATUS_PANEL_WIDTH -
            (HOST_GUI_CONTROL_GAP * (HOST_GUI_TOOLBAR_BUTTON_COUNT - 1.0))) /
        HOST_GUI_TOOLBAR_BUTTON_COUNT;

    NSFont* const terminal_font =
        [NSFont monospacedSystemFontOfSize:HOST_GUI_TERMINAL_FONT_SIZE weight:NSFontWeightRegular];
    terminal_view_ = make_text_view(
        terminal_font,
        [NSColor colorWithCalibratedRed:0.78 green:0.96 blue:0.78 alpha:1.0],
        [NSColor blackColor]);
    NSScrollView* const terminal_scroll = make_scroll_view(
        terminal_view_,
        NSMakeRect(HOST_GUI_PANEL_PADDING, terminal_y, terminal_width, terminal_height));
    [terminal_scroll setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content_view addSubview:terminal_scroll];

    NSFont* const status_font =
        [NSFont monospacedSystemFontOfSize:HOST_GUI_STATUS_FONT_SIZE weight:NSFontWeightRegular];
    status_view_ = make_text_view(
        status_font,
        [NSColor colorWithCalibratedWhite:0.88 alpha:1.0],
        [NSColor colorWithCalibratedWhite:0.12 alpha:1.0]);
    NSScrollView* const status_scroll = make_scroll_view(
        status_view_,
        NSMakeRect(right_panel_x, terminal_y, HOST_GUI_STATUS_PANEL_WIDTH, status_height));
    [status_scroll setAutoresizingMask:NSViewMinXMargin | NSViewHeightSizable];
    [content_view addSubview:status_scroll];

    input_field_ = [[NSTextField alloc] initWithFrame:
        NSMakeRect(HOST_GUI_PANEL_PADDING, input_y, input_width, HOST_GUI_INPUT_HEIGHT)];
    [input_field_ setDelegate:self];
    [input_field_ setTarget:self];
    [input_field_ setAction:@selector(submitCommand:)];
    [input_field_ setFont:terminal_font];
    [input_field_ setPlaceholderString:@"Command"];
    [input_field_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
    [content_view addSubview:input_field_];

    NSButton* const send_button = make_button(
        @"Send",
        self,
        @selector(submitCommand:),
        NSMakeRect(send_button_x, input_y, HOST_GUI_BUTTON_WIDTH, HOST_GUI_BUTTON_HEIGHT));
    [send_button setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
    [content_view addSubview:send_button];

    const CGFloat reset_button_x = right_panel_x;
    NSButton* const reset_button = make_button(
        @"Reset",
        self,
        @selector(resetMachine:),
        NSMakeRect(reset_button_x, toolbar_y, toolbar_button_width, HOST_GUI_BUTTON_HEIGHT));
    [reset_button setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [content_view addSubview:reset_button];

    const CGFloat pump_button_x = reset_button_x + toolbar_button_width + HOST_GUI_CONTROL_GAP;
    NSButton* const pump_button = make_button(
        @"Pump",
        self,
        @selector(pumpMachine:),
        NSMakeRect(pump_button_x, toolbar_y, toolbar_button_width, HOST_GUI_BUTTON_HEIGHT));
    [pump_button setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [content_view addSubview:pump_button];

    const CGFloat exit_button_x = pump_button_x + toolbar_button_width + HOST_GUI_CONTROL_GAP;
    NSButton* const exit_button = make_button(
        @"Exit Shell",
        self,
        @selector(sendExitCommand:),
        NSMakeRect(exit_button_x, toolbar_y, toolbar_button_width, HOST_GUI_BUTTON_HEIGHT));
    [exit_button setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [content_view addSubview:exit_button];
}

- (void)bootMachine
{
    try
    {
        session_->boot();
        [self renderFrame];
    }
    catch (const std::exception& error)
    {
        [self renderError:error.what()];
    }
}

- (void)renderFrame
{
    const mnos::host::HostDebuggerFrame frame = session_->frame();
    [window_ setTitle:ns_string_from_std(frame.title)];
    [terminal_view_ setString:ns_string_from_std(frame.display_text)];
    [status_view_ setString:ns_string_from_std(frame.summary_text)];
    [input_field_ setEnabled:frame.accepts_input];
    [terminal_view_ scrollRangeToVisible:NSMakeRange([[terminal_view_ string] length], 0)];
}

- (void)renderError:(const char*)message
{
    NSString* const error_text = message == nullptr ? @"runtime error" : [NSString stringWithUTF8String:message];
    [status_view_ setString:error_text];
    [input_field_ setEnabled:NO];
}

- (void)submitCommand:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    const std::string command = std_string_from_ns([input_field_ stringValue]);
    [input_field_ setStringValue:@""];
    static_cast<void>(session_->submit_command_line(command));
    [self renderFrame];
}

- (void)resetMachine:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    try
    {
        session_->reset();
        [self renderFrame];
    }
    catch (const std::exception& error)
    {
        [self renderError:error.what()];
    }
}

- (void)pumpMachine:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->pump_until_waiting());
    [self renderFrame];
}

- (void)sendExitCommand:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->submit_command_line("exit"));
    [self renderFrame];
}
@end

int main(const int argc, char** argv)
{
    if (has_flag(argc, argv, HOST_GUI_HELP_FLAG) || has_flag(argc, argv, HOST_GUI_SHORT_HELP_FLAG))
    {
        print_usage(std::cout);
        return HOST_GUI_SUCCESS_EXIT_CODE;
    }
    if (has_unknown_flag(argc, argv))
    {
        print_usage(std::cerr);
        return HOST_GUI_USAGE_ERROR_EXIT_CODE;
    }
    if (has_flag(argc, argv, HOST_GUI_SMOKE_FLAG))
    {
        return run_smoke(std::cout);
    }

    @autoreleasepool
    {
        NSApplication* const application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        MnosGuiController* const controller = [[MnosGuiController alloc] init];
        [application setDelegate:controller];
        [application run];
    }
    return HOST_GUI_SUCCESS_EXIT_CODE;
}
