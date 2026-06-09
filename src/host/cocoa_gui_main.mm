#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <mnos/host/debugger_session.hpp>
#include <mnos/host/debugger_text_layout.hpp>

namespace
{
constexpr std::string_view HOST_GUI_HELP_FLAG = "--help";
constexpr std::string_view HOST_GUI_SHORT_HELP_FLAG = "-h";
constexpr std::string_view HOST_GUI_SMOKE_FLAG = "--smoke";
constexpr std::string_view HOST_GUI_USAGE_TEXT = "usage: mnos_gui [--smoke]\n";
constexpr int HOST_GUI_SUCCESS_EXIT_CODE = 0;
constexpr int HOST_GUI_RUNTIME_ERROR_EXIT_CODE = 1;
constexpr int HOST_GUI_USAGE_ERROR_EXIT_CODE = 2;

constexpr CGFloat HOST_GUI_WINDOW_WIDTH = 1480.0;
constexpr CGFloat HOST_GUI_WINDOW_HEIGHT = 900.0;
constexpr CGFloat HOST_GUI_PANEL_PADDING = 8.0;
constexpr CGFloat HOST_GUI_LEFT_COLUMN_MIN_WIDTH = 640.0;
constexpr CGFloat HOST_GUI_RIGHT_COLUMN_MIN_WIDTH = 560.0;
constexpr CGFloat HOST_GUI_TERMINAL_MIN_HEIGHT = 360.0;
constexpr CGFloat HOST_GUI_OUTPUT_MIN_HEIGHT = 160.0;
constexpr CGFloat HOST_GUI_REGISTERS_MIN_HEIGHT = 210.0;
constexpr CGFloat HOST_GUI_INSTRUCTIONS_MIN_HEIGHT = 220.0;
constexpr CGFloat HOST_GUI_DETAIL_MIN_HEIGHT = 180.0;
constexpr CGFloat HOST_GUI_STATUS_STRIP_HEIGHT = 54.0;
constexpr CGFloat HOST_GUI_INPUT_HEIGHT = 30.0;
constexpr CGFloat HOST_GUI_COMMAND_LABEL_WIDTH = 74.0;
constexpr CGFloat HOST_GUI_STATUS_TILE_BORDER_WIDTH = 1.0;
constexpr CGFloat HOST_GUI_TOOLBAR_BUTTON_WIDTH = 78.0;
constexpr CGFloat HOST_GUI_SEND_BUTTON_WIDTH = 82.0;
constexpr CGFloat HOST_GUI_BUTTON_HEIGHT = 28.0;
constexpr CGFloat HOST_GUI_TOOLBAR_HEIGHT = 34.0;
constexpr CGFloat HOST_GUI_CONTROL_GAP = 8.0;
constexpr CGFloat HOST_GUI_PANE_LABEL_HEIGHT = 20.0;
constexpr CGFloat HOST_GUI_TEXT_CONTAINER_PADDING = 4.0;
constexpr CGFloat HOST_GUI_TERMINAL_FONT_SIZE = 14.0;
constexpr CGFloat HOST_GUI_STATUS_FONT_SIZE = 12.0;
constexpr CGFloat HOST_GUI_DETAIL_FONT_SIZE = 11.0;
constexpr CGFloat HOST_GUI_PANE_LABEL_FONT_SIZE = 11.0;
constexpr CGFloat HOST_GUI_TITLE_FONT_SIZE = 13.0;
constexpr CGFloat HOST_GUI_MIN_WINDOW_WIDTH = 1260.0;
constexpr CGFloat HOST_GUI_MIN_WINDOW_HEIGHT = 760.0;
constexpr CGFloat HOST_GUI_CHROME_WHITE = 0.86;
constexpr CGFloat HOST_GUI_TILE_WHITE = 0.92;
constexpr CGFloat HOST_GUI_TILE_BORDER_WHITE = 0.68;
constexpr CGFloat HOST_GUI_TEXT_WHITE = 0.08;
constexpr CGFloat HOST_GUI_SECONDARY_TEXT_WHITE = 0.28;
constexpr std::size_t HOST_GUI_DETAIL_WRAP_COLUMN_COUNT = std::size_t{58};
constexpr std::size_t HOST_GUI_DETAIL_WRAP_INDENT = std::size_t{4};
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

[[nodiscard]] NSColor* host_gui_chrome_color()
{
    return [NSColor colorWithCalibratedWhite:HOST_GUI_CHROME_WHITE alpha:1.0];
}

[[nodiscard]] NSColor* host_gui_tile_background_color()
{
    return [NSColor colorWithCalibratedWhite:HOST_GUI_TILE_WHITE alpha:1.0];
}

[[nodiscard]] NSColor* host_gui_tile_border_color()
{
    return [NSColor colorWithCalibratedWhite:HOST_GUI_TILE_BORDER_WHITE alpha:1.0];
}

[[nodiscard]] NSColor* host_gui_text_color()
{
    return [NSColor colorWithCalibratedWhite:HOST_GUI_TEXT_WHITE alpha:1.0];
}

[[nodiscard]] NSColor* host_gui_secondary_text_color()
{
    return [NSColor colorWithCalibratedWhite:HOST_GUI_SECONDARY_TEXT_WHITE alpha:1.0];
}

[[nodiscard]] mnos::host::HostTextLayoutConfig host_gui_detail_text_layout() noexcept
{
    mnos::host::HostTextLayoutConfig config;
    config.max_column_count = HOST_GUI_DETAIL_WRAP_COLUMN_COUNT;
    config.continuation_indent = HOST_GUI_DETAIL_WRAP_INDENT;
    return config;
}

[[nodiscard]] NSString* ns_wrapped_debugger_text(const std::string& text,
                                                 const mnos::host::HostTextLayoutConfig& layout)
{
    return ns_string_from_std(mnos::host::wrap_debugger_text(text, layout));
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

[[nodiscard]] NSTextField* make_title_field(NSString* title)
{
    NSTextField* const field = [NSTextField labelWithString:title];
    [field setFont:[NSFont systemFontOfSize:HOST_GUI_TITLE_FONT_SIZE weight:NSFontWeightSemibold]];
    [field setTextColor:host_gui_text_color()];
    [field setLineBreakMode:NSLineBreakByTruncatingTail];
    [field setTranslatesAutoresizingMaskIntoConstraints:NO];
    [field setContentHuggingPriority:NSLayoutPriorityDefaultLow
                      forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

[[nodiscard]] NSTextField* make_status_line_field(NSFont* font)
{
    NSTextField* const field = [NSTextField labelWithString:@""];
    [field setFont:font];
    [field setTextColor:host_gui_text_color()];
    [field setLineBreakMode:NSLineBreakByTruncatingTail];
    [field setTranslatesAutoresizingMaskIntoConstraints:NO];
    [field setContentHuggingPriority:NSLayoutPriorityDefaultLow
                      forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

[[nodiscard]] NSTextField* make_pane_label(NSString* title)
{
    NSTextField* const label = [NSTextField labelWithString:title];
    [label setFont:[NSFont systemFontOfSize:HOST_GUI_PANE_LABEL_FONT_SIZE weight:NSFontWeightSemibold]];
    [label setTextColor:host_gui_secondary_text_color()];
    [label setLineBreakMode:NSLineBreakByTruncatingTail];
    [label setTranslatesAutoresizingMaskIntoConstraints:NO];
    return label;
}

[[nodiscard]] NSView* make_labeled_pane(NSString* title, NSView* content_view)
{
    NSView* const pane = [[NSView alloc] initWithFrame:NSZeroRect];
    [pane setTranslatesAutoresizingMaskIntoConstraints:NO];

    NSTextField* const label = make_pane_label(title);
    [pane addSubview:label];
    [pane addSubview:content_view];

    [NSLayoutConstraint activateConstraints:@[
        [[label leadingAnchor] constraintEqualToAnchor:[pane leadingAnchor]],
        [[label trailingAnchor] constraintEqualToAnchor:[pane trailingAnchor]],
        [[label topAnchor] constraintEqualToAnchor:[pane topAnchor]],
        [[label heightAnchor] constraintEqualToConstant:HOST_GUI_PANE_LABEL_HEIGHT],

        [[content_view leadingAnchor] constraintEqualToAnchor:[pane leadingAnchor]],
        [[content_view trailingAnchor] constraintEqualToAnchor:[pane trailingAnchor]],
        [[content_view topAnchor] constraintEqualToAnchor:[label bottomAnchor]],
        [[content_view bottomAnchor] constraintEqualToAnchor:[pane bottomAnchor]]
    ]];
    return pane;
}

[[nodiscard]] NSView* make_status_tile(NSString* title, NSTextField* value)
{
    NSView* const tile = [[NSView alloc] initWithFrame:NSZeroRect];
    [tile setTranslatesAutoresizingMaskIntoConstraints:NO];
    [tile setWantsLayer:YES];
    [[tile layer] setBackgroundColor:[host_gui_tile_background_color() CGColor]];
    [[tile layer] setBorderColor:[host_gui_tile_border_color() CGColor]];
    [[tile layer] setBorderWidth:HOST_GUI_STATUS_TILE_BORDER_WIDTH];

    NSTextField* const title_field = make_pane_label(title);
    [value setTextColor:host_gui_text_color()];

    [tile addSubview:title_field];
    [tile addSubview:value];

    [NSLayoutConstraint activateConstraints:@[
        [[title_field leadingAnchor] constraintEqualToAnchor:[tile leadingAnchor]
                                                    constant:HOST_GUI_PANEL_PADDING],
        [[title_field trailingAnchor] constraintEqualToAnchor:[tile trailingAnchor]
                                                     constant:-HOST_GUI_PANEL_PADDING],
        [[title_field topAnchor] constraintEqualToAnchor:[tile topAnchor] constant:HOST_GUI_PANEL_PADDING],

        [[value leadingAnchor] constraintEqualToAnchor:[tile leadingAnchor] constant:HOST_GUI_PANEL_PADDING],
        [[value trailingAnchor] constraintEqualToAnchor:[tile trailingAnchor]
                                               constant:-HOST_GUI_PANEL_PADDING],
        [[value topAnchor] constraintEqualToAnchor:[title_field bottomAnchor]],
        [[value bottomAnchor] constraintLessThanOrEqualToAnchor:[tile bottomAnchor]
                                                       constant:-HOST_GUI_PANEL_PADDING]
    ]];
    return tile;
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

} // namespace

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
    const NSEventModifierFlags flags = [event modifierFlags] & HOST_GUI_DEVICE_INDEPENDENT_MODIFIER_MASK;
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

@interface MnosGuiController
    : NSObject <NSApplicationDelegate, NSTextFieldDelegate, MnosTerminalInputDelegate>
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
    NSTextField* status_state_field_;
    NSTextField* status_counters_field_;
    NSTextField* status_cursor_field_;
    NSTextField* input_field_;
    NSButton* reset_button_;
    NSButton* step_button_;
    NSButton* exec_button_;
    NSButton* run_button_;
    NSButton* pause_button_;
    NSButton* exit_button_;
    NSButton* send_button_;
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
    const NSRect window_frame = NSMakeRect(0.0, 0.0, HOST_GUI_WINDOW_WIDTH, HOST_GUI_WINDOW_HEIGHT);
    window_ =
        [[NSWindow alloc] initWithContentRect:window_frame
                                    styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                               NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    [window_ setTitle:@"MNOS Workbench"];
    [window_ setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
    [window_ setMinSize:NSMakeSize(HOST_GUI_MIN_WINDOW_WIDTH, HOST_GUI_MIN_WINDOW_HEIGHT)];
    [window_ center];

    NSView* const content_view = [window_ contentView];
    [content_view setWantsLayer:YES];
    [[content_view layer] setBackgroundColor:[host_gui_chrome_color() CGColor]];

    NSFont* const terminal_font = [NSFont monospacedSystemFontOfSize:HOST_GUI_TERMINAL_FONT_SIZE
                                                              weight:NSFontWeightRegular];
    NSFont* const status_font = [NSFont monospacedSystemFontOfSize:HOST_GUI_STATUS_FONT_SIZE
                                                            weight:NSFontWeightRegular];
    NSFont* const detail_font = [NSFont monospacedSystemFontOfSize:HOST_GUI_DETAIL_FONT_SIZE
                                                            weight:NSFontWeightRegular];

    NSStackView* const toolbar = [[NSStackView alloc] initWithFrame:NSZeroRect];
    [toolbar setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [toolbar setAlignment:NSLayoutAttributeCenterY];
    [toolbar setDistribution:NSStackViewDistributionFill];
    [toolbar setSpacing:HOST_GUI_CONTROL_GAP];
    [toolbar setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:toolbar];

    [toolbar addArrangedSubview:make_title_field(@"MNOS x86-64 Machine Debugger")];
    reset_button_ = make_toolbar_button(@"Reset", self, @selector(resetMachine:));
    step_button_ = make_toolbar_button(@"Step", self, @selector(stepMachine:));
    exec_button_ = make_toolbar_button(@"Exec", self, @selector(execUserProgram:));
    run_button_ = make_toolbar_button(@"Run", self, @selector(runMachine:));
    pause_button_ = make_toolbar_button(@"Pause", self, @selector(pauseMachine:));
    exit_button_ = make_toolbar_button(@"Exit", self, @selector(sendExitCommand:));
    [toolbar addArrangedSubview:reset_button_];
    [toolbar addArrangedSubview:step_button_];
    [toolbar addArrangedSubview:exec_button_];
    [toolbar addArrangedSubview:run_button_];
    [toolbar addArrangedSubview:pause_button_];
    [toolbar addArrangedSubview:exit_button_];

    NSStackView* const status_strip = [[NSStackView alloc] initWithFrame:NSZeroRect];
    [status_strip setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [status_strip setAlignment:NSLayoutAttributeCenterY];
    [status_strip setDistribution:NSStackViewDistributionFillEqually];
    [status_strip setSpacing:HOST_GUI_CONTROL_GAP];
    [status_strip setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:status_strip];

    status_state_field_ = make_status_line_field(status_font);
    status_counters_field_ = make_status_line_field(status_font);
    status_cursor_field_ = make_status_line_field(status_font);
    [status_strip addArrangedSubview:make_status_tile(@"State", status_state_field_)];
    [status_strip addArrangedSubview:make_status_tile(@"Counters", status_counters_field_)];
    [status_strip addArrangedSubview:make_status_tile(@"Cursor", status_cursor_field_)];

    NSSplitView* const workspace_split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    [workspace_split setVertical:YES];
    [workspace_split setDividerStyle:NSSplitViewDividerStyleThin];
    [workspace_split setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:workspace_split];

    NSSplitView* const left_column_split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    [left_column_split setVertical:NO];
    [left_column_split setDividerStyle:NSSplitViewDividerStyleThin];
    [left_column_split setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[left_column_split widthAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_LEFT_COLUMN_MIN_WIDTH]
        .active = YES;

    NSSplitView* const right_column_split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    [right_column_split setVertical:NO];
    [right_column_split setDividerStyle:NSSplitViewDividerStyleThin];
    [right_column_split setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[right_column_split widthAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_RIGHT_COLUMN_MIN_WIDTH]
        .active = YES;

    [workspace_split addArrangedSubview:left_column_split];
    [workspace_split addArrangedSubview:right_column_split];
    [workspace_split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:0];
    [workspace_split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:1];

    terminal_view_ = [[MnosTerminalTextView alloc] initWithFrame:NSZeroRect];
    [terminal_view_ setInputDelegate:self];
    configure_text_view(terminal_view_, terminal_font,
                        [NSColor colorWithCalibratedRed:0.78 green:0.94 blue:0.78 alpha:1.0],
                        [NSColor blackColor]);
    NSScrollView* const terminal_scroll = make_scroll_view(terminal_view_);
    NSView* const terminal_pane = make_labeled_pane(@"Guest Display", terminal_scroll);
    [[terminal_pane heightAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_TERMINAL_MIN_HEIGHT]
        .active = YES;

    trace_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(trace_view_, detail_font, [NSColor blackColor], [NSColor whiteColor]);
    NSView* const output_pane = make_labeled_pane(@"Debugger Output", make_scroll_view(trace_view_));
    [[output_pane heightAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_OUTPUT_MIN_HEIGHT].active =
        YES;

    [left_column_split addArrangedSubview:terminal_pane];
    [left_column_split addArrangedSubview:output_pane];
    [left_column_split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:0];
    [left_column_split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:1];

    status_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(status_view_, detail_font, [NSColor blackColor], [NSColor whiteColor]);

    registers_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(registers_view_, detail_font, [NSColor blackColor], [NSColor whiteColor]);
    NSView* const registers_pane = make_labeled_pane(@"CPU Registers", make_scroll_view(registers_view_));
    [[registers_pane heightAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_REGISTERS_MIN_HEIGHT]
        .active = YES;

    paging_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(paging_view_, detail_font, [NSColor blackColor], [NSColor whiteColor]);

    instruction_trace_view_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
    configure_text_view(instruction_trace_view_, detail_font, [NSColor blackColor], [NSColor whiteColor]);
    NSView* const instructions_pane =
        make_labeled_pane(@"Instruction Trace", make_scroll_view(instruction_trace_view_));
    [[instructions_pane heightAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_INSTRUCTIONS_MIN_HEIGHT]
        .active = YES;

    NSTabView* const detail_tab_view = [[NSTabView alloc] initWithFrame:NSZeroRect];
    [detail_tab_view setTranslatesAutoresizingMaskIntoConstraints:NO];
    add_text_tab(detail_tab_view, @"Paging", paging_view_);
    add_text_tab(detail_tab_view, @"Machine", status_view_);
    NSView* const detail_pane = make_labeled_pane(@"Paging / Machine State", detail_tab_view);
    [[detail_pane heightAnchor] constraintGreaterThanOrEqualToConstant:HOST_GUI_DETAIL_MIN_HEIGHT].active =
        YES;

    [right_column_split addArrangedSubview:registers_pane];
    [right_column_split addArrangedSubview:instructions_pane];
    [right_column_split addArrangedSubview:detail_pane];
    [right_column_split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:0];
    [right_column_split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:1];
    [right_column_split setHoldingPriority:NSLayoutPriorityDefaultHigh forSubviewAtIndex:2];

    NSView* const input_row = [[NSView alloc] initWithFrame:NSZeroRect];
    [input_row setTranslatesAutoresizingMaskIntoConstraints:NO];
    [content_view addSubview:input_row];

    NSTextField* const command_label = [NSTextField labelWithString:@"Command"];
    [command_label setFont:[NSFont systemFontOfSize:HOST_GUI_STATUS_FONT_SIZE weight:NSFontWeightSemibold]];
    [command_label setTextColor:host_gui_text_color()];
    [command_label setTranslatesAutoresizingMaskIntoConstraints:NO];
    [input_row addSubview:command_label];

    input_field_ = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [input_field_ setDelegate:self];
    [input_field_ setTarget:self];
    [input_field_ setAction:@selector(submitCommand:)];
    [input_field_ setFont:terminal_font];
    [input_field_ setPlaceholderString:@"Command"];
    [input_field_ setTranslatesAutoresizingMaskIntoConstraints:NO];
    [input_row addSubview:input_field_];

    send_button_ = make_send_button(self, @selector(submitCommand:));
    [input_row addSubview:send_button_];

    [NSLayoutConstraint activateConstraints:@[
        [[toolbar leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor]
                                                constant:HOST_GUI_PANEL_PADDING],
        [[toolbar trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor]
                                                 constant:-HOST_GUI_PANEL_PADDING],
        [[toolbar topAnchor] constraintEqualToAnchor:[content_view topAnchor]
                                            constant:HOST_GUI_PANEL_PADDING],
        [[toolbar heightAnchor] constraintEqualToConstant:HOST_GUI_TOOLBAR_HEIGHT],

        [[status_strip leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor]
                                                     constant:HOST_GUI_PANEL_PADDING],
        [[status_strip trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor]
                                                      constant:-HOST_GUI_PANEL_PADDING],
        [[status_strip topAnchor] constraintEqualToAnchor:[toolbar bottomAnchor]
                                                 constant:HOST_GUI_CONTROL_GAP],
        [[status_strip heightAnchor] constraintEqualToConstant:HOST_GUI_STATUS_STRIP_HEIGHT],

        [[input_row leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor]
                                                  constant:HOST_GUI_PANEL_PADDING],
        [[input_row trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor]
                                                   constant:-HOST_GUI_PANEL_PADDING],
        [[input_row bottomAnchor] constraintEqualToAnchor:[content_view bottomAnchor]
                                                 constant:-HOST_GUI_PANEL_PADDING],
        [[input_row heightAnchor] constraintEqualToConstant:HOST_GUI_INPUT_HEIGHT],

        [[workspace_split leadingAnchor] constraintEqualToAnchor:[content_view leadingAnchor]
                                                        constant:HOST_GUI_PANEL_PADDING],
        [[workspace_split trailingAnchor] constraintEqualToAnchor:[content_view trailingAnchor]
                                                         constant:-HOST_GUI_PANEL_PADDING],
        [[workspace_split topAnchor] constraintEqualToAnchor:[status_strip bottomAnchor]
                                                    constant:HOST_GUI_CONTROL_GAP],
        [[workspace_split bottomAnchor] constraintEqualToAnchor:[input_row topAnchor]
                                                       constant:-HOST_GUI_CONTROL_GAP],

        [[command_label leadingAnchor] constraintEqualToAnchor:[input_row leadingAnchor]],
        [[command_label centerYAnchor] constraintEqualToAnchor:[input_row centerYAnchor]],
        [[command_label widthAnchor] constraintEqualToConstant:HOST_GUI_COMMAND_LABEL_WIDTH],

        [[input_field_ leadingAnchor] constraintEqualToAnchor:[command_label trailingAnchor]
                                                     constant:HOST_GUI_CONTROL_GAP],
        [[input_field_ trailingAnchor] constraintEqualToAnchor:[send_button_ leadingAnchor]
                                                      constant:-HOST_GUI_CONTROL_GAP],
        [[input_field_ centerYAnchor] constraintEqualToAnchor:[input_row centerYAnchor]],
        [[input_field_ heightAnchor] constraintEqualToConstant:HOST_GUI_INPUT_HEIGHT],

        [[send_button_ trailingAnchor] constraintEqualToAnchor:[input_row trailingAnchor]],
        [[send_button_ centerYAnchor] constraintEqualToAnchor:[input_row centerYAnchor]]
    ]];
}

- (void)renderControls:(const mnos::host::HostDebuggerFrame&)frame
{
    [reset_button_ setEnabled:frame.controls.can_reset];
    [step_button_ setEnabled:frame.controls.can_step];
    [exec_button_ setEnabled:frame.controls.can_execute_user_program];
    [run_button_ setEnabled:frame.controls.can_run];
    [pause_button_ setEnabled:frame.controls.can_pause];
    [exit_button_ setEnabled:frame.controls.can_send_exit];
    [input_field_ setEnabled:frame.controls.can_submit_input];
    [send_button_ setEnabled:frame.controls.can_submit_input];
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
    const mnos::host::HostTextLayoutConfig detail_layout = host_gui_detail_text_layout();
    [window_ setTitle:ns_string_from_std(frame.title)];
    [terminal_view_ setString:ns_string_from_std(frame.display_text)];
    [status_state_field_ setStringValue:ns_string_from_std(frame.status_text)];
    [status_counters_field_ setStringValue:ns_string_from_std(frame.counters_text)];
    [status_cursor_field_ setStringValue:ns_string_from_std(frame.cursor_text)];
    [status_view_ setString:ns_wrapped_debugger_text(make_status_panel_text(frame), detail_layout)];
    [registers_view_ setString:ns_wrapped_debugger_text(frame.registers_text, detail_layout)];
    [paging_view_ setString:ns_wrapped_debugger_text(frame.paging_text, detail_layout)];
    [trace_view_ setString:ns_wrapped_debugger_text(frame.trace_text, detail_layout)];
    [instruction_trace_view_ setString:ns_wrapped_debugger_text(frame.instruction_trace_text, detail_layout)];
    [self renderControls:frame];
    [terminal_view_ scrollRangeToVisible:NSMakeRange([[terminal_view_ string] length], 0)];
    [registers_view_ scrollRangeToVisible:NSMakeRange(0, 0)];
    [paging_view_ scrollRangeToVisible:NSMakeRange(0, 0)];
    [trace_view_ scrollRangeToVisible:NSMakeRange([[trace_view_ string] length], 0)];
    [instruction_trace_view_ scrollRangeToVisible:NSMakeRange([[instruction_trace_view_ string] length], 0)];
}

- (void)renderError:(const char*)message
{
    NSString* const error_text =
        message == nullptr ? @"runtime error" : [NSString stringWithUTF8String:message];
    [status_view_ setString:error_text];
    [status_state_field_ setStringValue:error_text];
    [status_counters_field_ setStringValue:error_text];
    [status_cursor_field_ setStringValue:error_text];
    [registers_view_ setString:error_text];
    [paging_view_ setString:error_text];
    [trace_view_ setString:error_text];
    [instruction_trace_view_ setString:error_text];
    [input_field_ setEnabled:NO];
    [send_button_ setEnabled:NO];
    [reset_button_ setEnabled:YES];
    [step_button_ setEnabled:NO];
    [exec_button_ setEnabled:NO];
    [run_button_ setEnabled:NO];
    [pause_button_ setEnabled:NO];
    [exit_button_ setEnabled:NO];
}

- (void)submitHostCommandLine:(std::string_view)commandLine
{
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->submit_command_line(commandLine));
    [self renderFrame];
}

- (void)submitHostInputEvent:(const mnos::host::HostInputEvent&)event
{
    if (session_ == nullptr)
    {
        return;
    }

    static_cast<void>(session_->submit_input_event(event));
    [self renderFrame];
}

- (void)submitCommand:(id)sender
{
    (void)sender;
    const std::string command = std_string_from_ns([input_field_ stringValue]);
    [input_field_ setStringValue:@""];
    [self submitHostCommandLine:command];
}

- (void)terminalTextView:(MnosTerminalTextView*)terminalView submitText:(NSString*)text
{
    (void)terminalView;
    [self submitHostInputEvent:mnos::host::HostInputEvent::text(std_string_from_ns(text))];
}

- (void)terminalTextView:(MnosTerminalTextView*)terminalView submitSpecialKey:(mnos::host::HostSpecialKey)key
{
    (void)terminalView;
    [self submitHostInputEvent:mnos::host::HostInputEvent::special_key(key)];
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
    [self submitHostCommandLine:"exit"];
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
