/*
 *
 * Copyright 2024 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * This example demonstrates server streaming with explicit flow control using
 * GRPCUnaryProtoCall on macOS.
 *
 * With flowControlEnabled = YES, gRPC delivers one message at a time. After
 * processing each message the app calls -receiveNextMessage on the call object
 * to authorise delivery of the next one. This gives the application full
 * back-pressure control over how fast the server stream is consumed.
 *
 * RPC used: grpc.testing.TestService/StreamingOutputCall
 *   - Client sends a single request listing the response sizes it wants.
 *   - Server streams back one RMTStreamingOutputCallResponse per entry.
 */

#import "ViewController.h"

#if USE_FRAMEWORKS
#import <RemoteTest/Messages.pbobjc.h>
#import <RemoteTest/Test.pbrpc.h>
#else
#import "src/objective-c/examples/RemoteTestClient/Messages.pbobjc.h"
#import "src/objective-c/examples/RemoteTestClient/Test.pbrpc.h"
#endif

#import <GRPCClient/GRPCCallOptions.h>
#import <ProtoRPC/ProtoRPC.h>

static NSString *const kHost = @"grpc-test.sandbox.googleapis.com";
static NSString *const kStreamingOutputCallPath =
    @"/grpc.testing.TestService/StreamingOutputCall";

// Number of streamed responses to request from the server.
static const NSUInteger kResponseCount = 5;
// Size (bytes) of the payload requested in each server response.
static const int32_t kResponsePayloadSize = 100;

@interface ViewController () <GRPCProtoResponseHandler>
@end

@implementation ViewController {
  NSTextView *_outputView;
  NSButton *_startButton;
  NSButton *_cancelButton;
  NSTextField *_statusLabel;

  GRPCUnaryProtoCall *_activeCall;
  NSUInteger _receivedCount;
}

#pragma mark - View lifecycle

- (void)loadView {
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 440)];

  // --- Output scroll view ---
  NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 80, 600, 320)];
  scroll.hasVerticalScroller = YES;
  scroll.borderType = NSBezelBorder;
  scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  _outputView = [[NSTextView alloc] initWithFrame:scroll.bounds];
  _outputView.editable = NO;
  _outputView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  scroll.documentView = _outputView;
  [root addSubview:scroll];

  // --- Status label ---
  _statusLabel = [NSTextField labelWithString:@"Press \"Start Streaming\" to begin."];
  _statusLabel.frame = NSMakeRect(20, 52, 600, 20);
  _statusLabel.autoresizingMask = NSViewWidthSizable;
  [root addSubview:_statusLabel];

  // --- Buttons ---
  _startButton = [NSButton buttonWithTitle:@"Start Streaming"
                                    target:self
                                    action:@selector(startStreaming:)];
  _startButton.frame = NSMakeRect(20, 16, 140, 30);
  [root addSubview:_startButton];

  _cancelButton = [NSButton buttonWithTitle:@"Cancel"
                                     target:self
                                     action:@selector(cancelStreaming:)];
  _cancelButton.frame = NSMakeRect(172, 16, 80, 30);
  _cancelButton.enabled = NO;
  [root addSubview:_cancelButton];

  self.view = root;
}

- (void)viewDidLoad {
  [super viewDidLoad];
}

#pragma mark - Actions

- (IBAction)startStreaming:(id)sender {
  [_activeCall cancel];
  _activeCall = nil;
  _receivedCount = 0;

  [self clearOutput];
  [self appendLine:@"Configuring call with flowControlEnabled = YES …"];

  // Build a request asking for kResponseCount responses of kResponsePayloadSize bytes each.
  RMTStreamingOutputCallRequest *request = [RMTStreamingOutputCallRequest message];
  for (NSUInteger i = 0; i < kResponseCount; i++) {
    RMTResponseParameters *params = [RMTResponseParameters message];
    params.size = kResponsePayloadSize;
    [request.responseParametersArray addObject:params];
  }

  GRPCRequestOptions *requestOptions =
      [[GRPCRequestOptions alloc] initWithHost:kHost
                                          path:kStreamingOutputCallPath
                                        safety:GRPCCallSafetyDefault];

  // Enable flow control so messages are delivered one at a time.
  // Each message only arrives after -receiveNextMessage is called.
  GRPCMutableCallOptions *callOptions = [[GRPCMutableCallOptions alloc] init];
  callOptions.flowControlEnabled = YES;

  _activeCall = [[GRPCUnaryProtoCall alloc]
      initWithRequestOptions:requestOptions
                     message:request
             responseHandler:self
                 callOptions:callOptions
               responseClass:[RMTStreamingOutputCallResponse class]];

  [self appendLine:[NSString stringWithFormat:
      @"Requesting %lu responses of %d bytes each …", (unsigned long)kResponseCount,
      kResponsePayloadSize]];

  _startButton.enabled = NO;
  _cancelButton.enabled = YES;
  _statusLabel.stringValue = @"Streaming…";

  // -start internally calls -receiveNextMessage once to prime the first delivery.
  [_activeCall start];
}

- (IBAction)cancelStreaming:(id)sender {
  [_activeCall cancel];
  _activeCall = nil;
  [self appendLine:@"— Call cancelled by user —"];
  [self resetButtons];
}

#pragma mark - GRPCProtoResponseHandler

- (dispatch_queue_t)dispatchQueue {
  return dispatch_get_main_queue();
}

- (void)didReceiveInitialMetadata:(NSDictionary *)initialMetadata {
  [self appendLine:@"Initial metadata received."];
}

- (void)didReceiveProtoMessage:(GPBMessage *)message {
  _receivedCount++;
  RMTStreamingOutputCallResponse *response = (RMTStreamingOutputCallResponse *)message;
  [self appendLine:[NSString stringWithFormat:@"[%lu/%lu] Received %lu bytes",
                    (unsigned long)_receivedCount,
                    (unsigned long)kResponseCount,
                    (unsigned long)response.payload.body.length]];

  // Explicitly authorise delivery of the next message.
  // Without this call the stream would stall here indefinitely.
  [_activeCall receiveNextMessage];
}

- (void)didCloseWithTrailingMetadata:(NSDictionary *)trailingMetadata error:(NSError *)error {
  if (error) {
    [self appendLine:[NSString stringWithFormat:@"Stream closed with error: %@",
                      error.localizedDescription]];
    _statusLabel.stringValue = @"Error — see log above.";
  } else {
    [self appendLine:[NSString stringWithFormat:
                          @"Stream complete. %lu/%lu messages received.",
                          (unsigned long)_receivedCount, (unsigned long)kResponseCount]];
    _statusLabel.stringValue = @"Done.";
  }
  _activeCall = nil;
  [self resetButtons];
}

#pragma mark - Helpers

- (void)appendLine:(NSString *)line {
  NSString *text = [line stringByAppendingString:@"\n"];
  [_outputView.textStorage appendAttributedString:[[NSAttributedString alloc] initWithString:text]];
  [_outputView scrollRangeToVisible:NSMakeRange(_outputView.string.length, 0)];
}

- (void)clearOutput {
  [_outputView.textStorage
      replaceCharactersInRange:NSMakeRange(0, _outputView.string.length)
          withAttributedString:[[NSAttributedString alloc] initWithString:@""]];
}

- (void)resetButtons {
  _startButton.enabled = YES;
  _cancelButton.enabled = NO;
}

@end
