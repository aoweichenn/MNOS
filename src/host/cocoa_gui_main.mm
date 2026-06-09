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
constexpr std::string_view HOST_GUI_TOOLBAR_STATUS_SEPARATOR = " | ";

constexpr CGFloat HOST_GUI_WINDOW_WIDTH = 1180.0;
constexpr CGFloat HOST_GUI_WINDOW_HEIGHT = 760.0;
constexpr CGFloat HOST_GUI_PANEL_PADDING = 10.0;
constexpr CGFloat HOST_GUI_INSPECTOR_MIN_WIDTH = 330.0;
constexpr CGFloat HOST_GUI_INSPECTOR_MAX_WIDTH = 420.0;
constexpr CGFloat HOST_GUI_TERMINAL_MIN_WIDTH = 560.0;
constexpr CGFloat HOST_GUI_STATUS_PANEL_HEIGHT = 190.0;
constexpr CGFloat HOST_GUI_INPUT_HEIGHT = 30.0;
constexpr CGFloat HOST_GUI_TOOLBAR_BUTTON_WIDTH = 76.0;
constexpr CGFloat HOST_GUI_SEND_BUTTON_WIDTH = 88.0;
constexpr CGFloat HOST_GUI_BUTTON_HEIGHT = 28.0;
constexpr CGFloat HOST_GUI_TOOLBAR_HEIGHT = 34.0;
constexpr CGFloat HOST_GUI_CONTROL_GAP = 8.0;
constexpr CGFloat HOST_GUI_TEXT_CONTAINER_PADDING = 4.0;
constexpr CGFloat HOST_GUI_TERMINAL_FONT_SIZE = 13.0;
constexpr CGFloat HOST_GUI_STATUS_FONT_SIZE = 12.0;
constexpr CGFloat HOST_GUI_MIN_WINDOW_WIDTH = 940.0;
constexpr CGFloat HOST_GUI_MIN_WINDOW_HEIGHT = 640.0;
constexpr NSEventModifierFlags HOST_GUI_DEVICE_INDEPENDENT_MODIFIER_MASK =
    NSEventModifierFlagDeviceIndependentFlagsMask;
constexpr unichar HOST_GUI_ESCAPE_CHARACTER = unichar{0x001B};

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
    static_cast<void>(session.run_sample_user_program());
    const mnos::host::HostDebuggerFrame frame = session.frame();
    output << frame.summary_text << '\n';
    return frame.snapshot.command_count == std::size_t{1} &&
            frame.display_text.find("memory_pages total=") != std::string::npos &&
            frame.instruction_trace_text.find("opcode=SYSCALL") != std::string::npos
        ? HOST_GUI_SUCCESS_EXIT_CODE
        : HOST_GUI_RUNTIME_ERROR_EXIT_CODE;
}

[[nodiscard]] std::string make_status_panel_text(const mnos::host::HostDebuggerFrame& frame)
{
    std::string text;
    text.reserve(frame.summary_text.size());
    text.append(frame.title);
    text.push_back('\n');
    text.append(frame.run_control_text);
    text.push_back('\n');
    text.append(frame.status_text);
    text.push_back('\n');
    text.append(frame.counters_text);
    text.push_back('\n');
    text.append(frame.memory_text);
    text.push_back('\n');
    text.append(frame.processor_text);
    text.push_back('\n');
    text.append(frame.cursor_text);
    text.push_back('\n');
    text.append(frame.cpu_text);
    return text;
}

[[nodiscard]] std::string make_toolbar_status_text(const mnos::host::HostDebuggerFrame& frame)
{
    std::string text;
    text.reserve(
        frame.status_text.size() +
        frame.counters_text.size() +
        frame.cursor_text.size() +
        (HOST_GUI_TOOLBAR_STATUS_SEPARATOR.size() * 2U));
    text.append(frame.status_text);
    text.append(HOST_GUI_TOOLBAR_STATUS_SEPARATOR);
    text.append(frame.counters_text);
    text.append(HOST_GUI_TOOLBAR_STATUS_SEPARATOR);
    text.append(frame.cursor_text);
    return text;
}

[[nodiscard]] NSButton* make_button(NSString* title, id target, SEL action, const CGFloat width)
{
    NSButton* const button = [[NSButton alloc] initWithFrame:NSZeroRect];
    [button setTitle:title];
    [button setTarget:target];
    [button setAction:action];
    [button setBezelStyle:NSBezelStyleTexturedRounded];
    [button setControlSize:NSControlSizeRegular];
    [button setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[button widthAnchor] constraintEqualToConstant:width].active = YES;
    [[button heightAnchor] constraintEqualToConstant:HOST_GUI_BUTTON_HEIGHT].active = YES;
    return button;
}

[[nodiscard]] NSButton* make_send_button(id target, SEL action)
{
    return make_button(@"Send", target, action, HOST_GUI_SEND_BUTTON_WIDTH);
}

[[nodiscard]] NSButton* make_toolbar_button(NSString* title, id target, SEL action)
{
    return make_button(title, target, action, HOST_GUI_TOOLBAR_BUTTON_WIDTH);
}

[[nodiscard]] NSTextField* make_status_line_field(NSFont* font)
{
    NSTextField* const field = [NSTextField labelWithString:@""];
    [field setFont:font];
    [field setTextColor:[NSColor colorWithCalibratedRed:0.84 green:0.88 blue:0.90 alpha:1.0]];
    [field setLineBreakMode:NSLineBreakByTruncatingTail];
    [field setTranslatesAutoresizingMaskIntoConstraints:NO];
    [field setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

[[nodiscard]] NSScrollView* make_scroll_view(NSView* document_view)
{
    NSScrollView* const scroll_view = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    [scroll_view setDocumentView:document_view];
    [scroll_view setHasVerticalScroller:YES];
    [scroll_view setHasHorizontalScroller:YES];
    [scroll_view setAutohidesScrollers:NO];
    [scroll_view setBorderType:NSBezelBorder];
    [scroll_view setTranslatesAutoresizingMaskIntoConstraints:NO];
    return scroll_view;
}

void configure_text_view(NSTextView* text_view, NSFont* font, NSColor* text_color, NSColor* background_color)
{
    [text_view setEditable:NO];
    [text_view setSelectable:YES];
    [text_view setFont:font];
    [text_view setTextColor:text_color];
    [text_view setBackgroundColor:background_color];
    [text_view setAutomaticQuoteSubstitutionEnabled:NO];
    [text_view setAutomaticDashSubstitutionEnabled:NO];
    [text_view setAutomaticTextReplacementEnabled:NO];
    [text_view setHorizontallyResizable:YES];
    [text_view setVerticallyResizable:YES];
    [[text_view textContainer] setContainerSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
    [[text_view textContainer] setWidthTracksTextView:NO];
    [[text_view textContainer] setLineFragmentPadding:HOST_GUI_TEXT_CONTAINER_PADDING];
}

void add_text_tab(NSTabView* tab_view, NSString* label, NSTextView* text_view)
{
    NSScrollView* const scroll_view = make_scroll_view(text_view);
    [scroll_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    NSTabViewItem* const item = [[NSTabViewItem alloc] initWithIdentifier:label];
    [item setLabel:label];
    [item setView:scroll_view];
    [tab_view addTabViewItem:item];
}

}

@class MnosTerminalTextView;

@protocol MnosTerminalInputDelegate
- (void)terminalTextView:(MnosTerminalTextView*)terminalView submitText:(NSString*)text;
- (void)terminalTextView:(MnosTerminalTextView*)terminalView submitSpecialKey:(mnos::host::HostSpecialKey)key;
@end

@interface MnosTerminalTextView : NSTextView
@property(nonatomic, weak) id<MnosTerminalInputDelegate> inputDelegate;
@end

@implementation MnosTerminalTextView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    [[self window] makeFirstResponder:self];
    [super mouseDown:event];
}

- (void)keyDown:(NSEvent*)event
{
    const NSEventModifierFlags flags =
        [event modifierFlags] & HOST_GUI_DEVICE_INDEPENDENT_MODIFIER_MASK;
    if ((flags & NSEventModifierFlagCommand) != 0U)
    {
        [super keyDown:event];
        return;
    }

    NSString* const characters_ignoring_modifiers = [event charactersIgnoringModifiers];
    if ([characters_ignoring_modifiers length] == 0U)
    {
        [super keyDown:event];
        return;
    }

    const unichar key = [characters_ignoring_modifiers characterAtIndex:0U];
    const bool control_down = (flags & NSEventModifierFlagControl) != 0U;
    if (control_down && (key == 'c' || key == 'C'))
    {
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::CTRL_C];
        return;
    }
    if (control_down && (key == 'd' || key == 'D'))
    {
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::CTRL_D];
        return;
    }
    if (control_down)
    {
        [super keyDown:event];
        return;
    }

    switch (key)
    {
    case NSCarriageReturnCharacter:
    case NSEnterCharacter:
    case NSNewlineCharacter:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::ENTER];
        return;
    case NSBackspaceCharacter:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::BACKSPACE];
        return;
    case NSDeleteCharacter:
    case NSDeleteFunctionKey:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::DELETE_KEY];
        return;
    case NSTabCharacter:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::TAB];
        return;
    case HOST_GUI_ESCAPE_CHARACTER:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::ESCAPE];
        return;
    case NSUpArrowFunctionKey:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::ARROW_UP];
        return;
    case NSDownArrowFunctionKey:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::ARROW_DOWN];
        return;
    case NSLeftArrowFunctionKey:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::ARROW_LEFT];
        return;
    case NSRightArrowFunctionKey:
        [self.inputDelegate terminalTextView:self submitSpecialKey:mnos::host::HostSpecialKey::ARROW_RIGHT];
        return;
    default:
        break;
    }

    NSString* const characters = [event characters];
    if ([characters length] > 0U)
    {
        [self.inputDelegate terminalTextView:self submitText:characters];
        return;
    }
    [super keyDown:event];
}
@end

@interface MnosGuiController : NSObject <NSApplicationDelegate, NSTextFieldDelegate, MnosTerminalInputDelegate>
@end

@implementation MnosGuiController
{
    NSWindow* window_;
    MnosTerminalTextView* terminal_view_;
    NSTextView* status_view_;
    NSTextView* registers_view_;
    NSTextView* paging_view_;
    NSTextView* trace_view_;
    NSTextView* instruction_trace_view_;
    NSTextField* status_line_field_;
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
    [window_ makeFirstResponder:terminal_view_];
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
    [window_ setTitle:@"MNOS Workbench"];
    [window_ setMinSize:NSMakeSize(HOST_GUI_MIN_WINDOW_WIDTH, HOST_GUI_MIN_WINDOW_HEIGHT)];
    [window_ center];

    NSView* const content_view = [window_ contentView];
    [content_view setWantsLayer:YES];
    [[content_view layer] setBackgroundColor:[[NSColor colorWithCalibratedWhite:0.07 alpha:1.0] CGColor]];

    NSFont* const terminal_font =
        [NSFont monospacedSystemFontOfSize:HOST_GUI_TERMINAL_FONT_SIZE weight:NSFontWeightRegular];
    NSFont* const status_font =
        [NSFont monospacedSystemFontOfSize:HOST_GUI_STATUS_FONT_SIZE weight:NSFontWeightRegular];

    NSStackView* const toolbar = [[NSStackView alloc] initWithFrame:NSZeroRect];
    [toolbar setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [toolbar setAlignment:NSLayoutAttributeCenterY];
    [toolbar setDistribution:NSStackViewDistributionFill];
    [toolbar setSpacing:HOST_GUI_CONTROL_GAP];
    [toolbar setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:toolbar];

    [toolbar addArrangedSubview:make_toolbar_button(@"Reset", self, @selector(resetMachine:))];
    [toolbar addArrangedSubview:make_toolbar_button(@"Step", self, @selector(stepMachine:))];
    [toolbar addArrangedSubview:make_toolbar_button(@"Exec", self, @selector(execUserProgram:))];
    [toolbar addArrangedSubview:make_toolbar_button(@"Run", self, @selector(runMachine:))];
    [toolbar addArrangedSubview:make_toolbar_button(@"Pause", self, @selector(pauseMachine:))];
    [toolbar addArrangedSubview:make_toolbar_button(@"Exit", self, @selector(sendExitCommand:))];

    status_line_field_ = make_status_line_field(status_font);
    [toolbar addArrangedSubview:status_line_field_];

    NSSplitView* const main_split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    [main_split setVertical:YES];
    [main_split setDividerStyle:NSSplitViewDividerStyleThin];
    [main_split setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:main_split];

    terminal_view_ = [[MnosTerminalTextView alloc] initWithFrame:NSZeroRect];
    [terminal_view_ setInputDelegate:self];
    configure_text_view(
        terminal_view_,
        terminal_font,
        [NSColor colorWithCalibratedRed:0.86 green:0.90 blue:0.92 alpha:1.0],
        [NSColor colorWithCalibratedRed:0.06 green:0.07 blue:0.09 alpha:1.0]);
    NSScrollView* const terminal_scroll = make_scroll_view(terminal_view_);
    [[terminal_scroll widthAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_TERMINAL_MIN_WIDTH].active = YES;

    NSView* const inspector_panel = [[NSView alloc] initWithFrame:NSZeroRect];
    [inspector_panel setTranslatesAutoresizingMaskIntoConstraints:NO];
    [inspector_panel setWantsLayer:YES];
    [[inspector_panel layer] setBackgroundColor:[[NSColor colorWithCalibratedWhite:0.09 alpha:1.0] CGColor]];
    [[inspector_panel widthAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_INSPECTOR_MIN_WIDTH].active = YES;
    [[inspector_panel widthAnchor] constraintLessThanOrEqualToConstant:HOST_GUI_INSPECTOR_MAX_WIDTH].active = YES;

    [main_split addArrangedSubview:terminal_scroll];
    [main_split addArrangedSubview:inspector_panel];
    [main_split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:0];
    [main_split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:1];

    status_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(
        status_view_,
        status_font,
        [NSColor colorWithCalibratedRed:0.82 green:0.86 blue:0.90 alpha:1.0],
        [NSColor colorWithCalibratedRed:0.09 green:0.11 blue:0.14 alpha:1.0]);
    NSScrollView* const status_scroll = make_scroll_view(status_view_);

    NSTabView* const detail_tab_view = [[NSTabView alloc] initWithFrame:NSZeroRect];
    [detail_tab_view setTranslatesAutoresizingMaskIntoConstraints:NO];
    [inspector_panel addSubview:status_scroll];
    [inspector_panel addSubview:detail_tab_view];

    registers_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(
        registers_view_,
        status_font,
        [NSColor colorWithCalibratedRed:0.86 green:0.92 blue:0.84 alpha:1.0],
        [NSColor colorWithCalibratedRed:0.08 green:0.10 blue:0.12 alpha:1.0]);
    add_text_tab(detail_tab_view, @"CPU", registers_view_);

    paging_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(
        paging_view_,
        status_font,
        [NSColor colorWithCalibratedRed:0.95 green:0.84 blue:0.70 alpha:1.0],
        [NSColor colorWithCalibratedRed:0.08 green:0.10 blue:0.12 alpha:1.0]);
    add_text_tab(detail_tab_view, @"Paging", paging_view_);

    trace_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(
        trace_view_,
        status_font,
        [NSColor colorWithCalibratedRed:0.80 green:0.88 blue:1.0 alpha:1.0],
        [NSColor colorWithCalibratedRed:0.08 green:0.10 blue:0.12 alpha:1.0]);
    add_text_tab(detail_tab_view, @"Actions", trace_view_);

    instruction_trace_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(
        instruction_trace_view_,
        status_font,
        [NSColor colorWithCalibratedRed:0.78 green:0.94 blue:0.91 alpha:1.0],
        [NSColor colorWithCalibratedRed:0.08 green:0.10 blue:0.12 alpha:1.0]);
    add_text_tab(detail_tab_view, @"Instructions", instruction_trace_view_);

    NSView* const input_row = [[NSView alloc] initWithFrame:NSZeroRect];
    [input_row setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:input_row];

    input_field_ = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [input_field_ setDelegate:self];
    [input_field_ setTarget:self];
    [input_field_ setAction:@selector(submitCommand:)];
    [input_field_ setFont:terminal_font];
    [input_field_ setPlaceholderString:@"Command"];
    [input_field_ setTranslatesAutoresizingMaskIntoConstraints:NO];
    [input_row addSubview:input_field_];

    NSButton* const send_button = make_send_button(self, @selector(submitCommand:));
    [input_row addSubview:send_button];

    [NSLayoutConstraint activateConstraints:@[
        [[toolbar leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor] constant:HOST_GUI_PANEL_PADDING],
        [[toolbar trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor] constant:-HOST_GUI_PANEL_PADDING],
        [[toolbar topAnchor] constraintEqualToAnchor:[content_view topAnchor] constant:HOST_GUI_PANEL_PADDING],
        [[toolbar heightAnchor] constraintEqualToConstant:HOST_GUI_TOOLBAR_HEIGHT],

        [[input_row leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor] constant:HOST_GUI_PANEL_PADDING],
        [[input_row trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor] constant:-HOST_GUI_PANEL_PADDING],
        [[input_row bottomAnchor] constraintEqualToAnchor:[content_view bottomAnchor] constant:-HOST_GUI_PANEL_PADDING],
        [[input_row heightAnchor] constraintEqualToConstant:HOST_GUI_INPUT_HEIGHT],

        [[main_split leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor] constant:HOST_GUI_PANEL_PADDING],
        [[main_split trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor] constant:-HOST_GUI_PANEL_PADDING],
        [[main_split topAnchor] constraintEqualToAnchor:[toolbar bottomAnchor] constant:HOST_GUI_CONTROL_GAP],
        [[main_split bottomAnchor] constraintEqualToAnchor:[input_row topAnchor] constant:-HOST_GUI_CONTROL_GAP],

        [[status_scroll leadingAnchor] constraintEqualToAnchor:[inspector_panel leadingAnchor]],
        [[status_scroll trailingAnchor] constraintEqualToAnchor:[inspector_panel trailingAnchor]],
        [[status_scroll topAnchor] constraintEqualToAnchor:[inspector_panel topAnchor]],
        [[status_scroll heightAnchor] constraintEqualToConstant:HOST_GUI_STATUS_PANEL_HEIGHT],

        [[detail_tab_view leadingAnchor] constraintEqualToAnchor:[inspector_panel leadingAnchor]],
        [[detail_tab_view trailingAnchor] constraintEqualToAnchor:[inspector_panel trailingAnchor]],
        [[detail_tab_view topAnchor] constraintEqualToAnchor:[status_scroll bottomAnchor] constant:HOST_GUI_CONTROL_GAP],
        [[detail_tab_view bottomAnchor] constraintEqualToAnchor:[inspector_panel bottomAnchor]],

        [[input_field_ leadingAnchor] constraintEqualToAnchor:[input_row leadingAnchor]],
        [[input_field_ trailingAnchor] constraintEqualToAnchor:[send_button leadingAnchor] constant:-HOST_GUI_CONTROL_GAP],
        [[input_field_ centerYAnchor] constraintEqualToAnchor:[input_row centerYAnchor]],
        [[input_field_ heightAnchor] constraintEqualToConstant:HOST_GUI_INPUT_HEIGHT],

        [[send_button trailingAnchor] constraintEqualToAnchor:[input_row trailingAnchor]],
        [[send_button centerYAnchor] constraintEqualToAnchor:[input_row centerYAnchor]]
    ]];
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
    [status_line_field_ setStringValue:ns_string_from_std(make_toolbar_status_text(frame))];
    [status_view_ setString:ns_string_from_std(make_status_panel_text(frame))];
    [registers_view_ setString:ns_string_from_std(frame.registers_text)];
    [paging_view_ setString:ns_string_from_std(frame.paging_text)];
    [trace_view_ setString:ns_string_from_std(frame.trace_text)];
    [instruction_trace_view_ setString:ns_string_from_std(frame.instruction_trace_text)];
    [input_field_ setEnabled:frame.accepts_input];
    [terminal_view_ scrollRangeToVisible:NSMakeRange([[terminal_view_ string] length], 0)];
    [registers_view_ scrollRangeToVisible:NSMakeRange(0, 0)];
    [paging_view_ scrollRangeToVisible:NSMakeRange(0, 0)];
    [trace_view_ scrollRangeToVisible:NSMakeRange([[trace_view_ string] length], 0)];
    [instruction_trace_view_ scrollRangeToVisible:NSMakeRange([[instruction_trace_view_ string] length], 0)];
}

- (void)renderError:(const char*)message
{
    NSString* const error_text = message == nullptr ? @"runtime error" : [NSString stringWithUTF8String:message];
    [status_view_ setString:error_text];
    [status_line_field_ setStringValue:error_text];
    [registers_view_ setString:error_text];
    [paging_view_ setString:error_text];
    [trace_view_ setString:error_text];
    [instruction_trace_view_ setString:error_text];
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

- (void)terminalTextView:(MnosTerminalTextView*)terminalView submitText:(NSString*)text
{
    (void)terminalView;
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->submit_text(std_string_from_ns(text)));
    [self renderFrame];
}

- (void)terminalTextView:(MnosTerminalTextView*)terminalView submitSpecialKey:(mnos::host::HostSpecialKey)key
{
    (void)terminalView;
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->submit_special_key(key));
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

- (void)stepMachine:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->step_until_waiting());
    [self renderFrame];
}

- (void)execUserProgram:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    try
    {
        static_cast<void>(session_->run_sample_user_program());
        [self renderFrame];
    }
    catch (const std::exception& error)
    {
        [self renderError:error.what()];
    }
}

- (void)runMachine:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->run_until_waiting());
    [self renderFrame];
}

- (void)pauseMachine:(id)sender
{
    (void)sender;
    if (session_ == nullptr)
    {
        return;
    }

    session_->pause();
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
