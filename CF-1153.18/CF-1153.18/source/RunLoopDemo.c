//
//  RunLoopDemo.c
//  CF-1153.18
//
//  Created by snow on 2019/5/14.
//  Copyright © 2019 snow. All rights reserved.
//

#include <stdio.h>

/*! A structure holding various bits of context for the run loop callbacks.
 */

struct SuperConnectContext {
    uint32_t        magic;          ///< Must be kSuperConnectContextMagic.
    int            openCount;      ///< The number of times that the kCFStreamEventOpenCompleted event has been received.
    CFErrorRef      error;          ///< The first error noticed by the connection; must be released.
};
typedef struct SuperConnectContext SuperConnectContext;

enum {
    kSuperConnectContextMagic = 0xfaffd00d
};

/*! A private run loop mode used by all the run loop callbacks.
 */

static const CFStringRef kSuperConnectRunLoopMode = CFSTR("com.example.apple-samplecode.SuperConnect");

/*! Common code used by the read and write stream run loop callbacks.
 *  \param type The type of stream event received.
 *  \param info The stream callback's info pointer, which is really a pointer to a SuperConnectContext structure.
 *  \param copyStreamErrorBlock A block that's called to extract the error from the stream when the event
 *  is kCFStreamEventErrorOccurred.
 */

static void StreamCallbackCommon(CFStreamEventType type, void * info, CFErrorRef (^copyStreamErrorBlock)()) {
    SuperConnectContext *  context;
    
    context = (SuperConnectContext *) info;
    assert(context->magic == kSuperConnectContextMagic);
    
    switch (type) {
        case kCFStreamEventOpenCompleted: {
            context->openCount += 1;
        } break;
        case kCFStreamEventEndEncountered: {
            if (context->error == NULL) {
                context->error = CFErrorCreate(NULL, kCFErrorDomainPOSIX, EPIPE, NULL);
            }
        } break;
        case kCFStreamEventErrorOccurred:{
            if (context->error == NULL) {
                context->error = copyStreamErrorBlock();
            }
        } break;
    }
}

/*! A read stream callback.  This just forwards the event to StreamCallbackCommon.
 *  \param stream The stream that received the event.
 *  \param type The type of stream event.
 *  \param info The info pointer for this callabck.
 */

static void ReadStreamCallback(CFReadStreamRef stream, CFStreamEventType type, void * info) {
    StreamCallbackCommon(type, info, ^{
        return CFReadStreamCopyError(stream);
    });
}

/*! A write stream callback.  This just forwards the event to StreamCallbackCommon.
 *  \param stream The stream that received the event.
 *  \param type The type of stream event.
 *  \param info The info pointer for this callabck.
 */

static void WriteStreamCallback(CFWriteStreamRef stream, CFStreamEventType type, void * info) {
    StreamCallbackCommon(type, info, ^{
        return CFWriteStreamCopyError(stream);
    });
}

/*! The run loop callback for the deadline timer.  This simply records the error in the context.
 *  \param timer The timer that fired.
 *  \param info The timer's info pointer, which is really a pointer to a SuperConnectContext structure.
 */

static void DeadlineTimerCallBack(CFRunLoopTimerRef timer, void *info) {
#pragma unused(timer)
    SuperConnectContext *  context;
    
    context = (SuperConnectContext *) info;
    assert(context->magic == kSuperConnectContextMagic);
    
    // Record the error.
    
    if (context->error == NULL) {
        context->error = CFErrorCreate(NULL, kCFErrorDomainPOSIX, ETIMEDOUT, NULL);
    }
    
    // This is required to get the CFRunLoopRunInMode call in RunRunLoopWithDeadline to return
    // so that it can notice that context->error is set.
    
    CFRunLoopStop(CFRunLoopGetCurrent());
}

/*! Creates and configures streams that will connect to the specified port on the specified host.
 *
 *  IMPORTANT: This routine returns streams even when it fails.
 *  \param context A pointer to the context used by the run loop callbacks.
 *  \param hostName The host to connect to.
 *  \param port The port on that host.
 *  \param readStreamPtr A pointer to a stream; the pointer itself must not be NULL;
 *  on entry, the pointed-to value is ignored; on return, it's set to the newly-created
 *  stream.
 *  \param writeStreamPtr A pointer to a stream; the pointer itself must not be NULL;
 *  on entry, the pointed-to value is ignored; on return, it's set to the newly-created
 *  stream.
 *  \returns On success, 0; on error, an errno-style error number.
 */

static errno_t CreateAndOpenStreams(SuperConnectContext * context, CFStringRef hostName, int port, CFReadStreamRef * readStreamPtr, CFWriteStreamRef * writeStreamPtr) {
    Boolean                success;
    CFReadStreamRef        readStream;
    CFWriteStreamRef        writeStream;
    CFStreamClientContext  streamContext = { 0, context, NULL, NULL, NULL };
    
    CFStreamCreatePairWithSocketToHost(
                                       NULL,
                                       hostName,
                                       (UInt32) port,
                                       &readStream,
                                       &writeStream
                                       );
    
    success = CFReadStreamSetClient(  readStream, kCFStreamEventOpenCompleted | kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,  ReadStreamCallback, &streamContext);
    assert(success);
    success = CFWriteStreamSetClient(writeStream, kCFStreamEventOpenCompleted | kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered, WriteStreamCallback, &streamContext);
    assert(success);
    
    CFReadStreamScheduleWithRunLoop(  readStream, CFRunLoopGetCurrent(), kSuperConnectRunLoopMode);
    CFWriteStreamScheduleWithRunLoop(writeStream, CFRunLoopGetCurrent(), kSuperConnectRunLoopMode);
    
    success = CFReadStreamOpen(readStream);
    if (success) {
        success = CFWriteStreamOpen(writeStream);
    }
    *readStreamPtr = readStream;
    *writeStreamPtr = writeStream;
    return success ? 0 : EINVAL;
}

/*! Runs the run loop until the connection completes, fails or times out.
 *  \param context A pointer to the context used by the run loop callbacks.
 *  \param deadline The timeout deadline.
 */

static void RunRunLoopWithDeadline(SuperConnectContext * context, CFAbsoluteTime deadline) {
    CFRunLoopTimerContext  timerContext  = { 0, context, NULL, NULL, NULL };
    CFRunLoopTimerRef      timer;
    
    timer = CFRunLoopTimerCreate(NULL, deadline, 0.0, 0, 0, DeadlineTimerCallBack, &timerContext);
    assert(timer != NULL);
    
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kSuperConnectRunLoopMode);
    
    do {
        (void) CFRunLoopRunInMode(kSuperConnectRunLoopMode, DBL_MAX, true);
        if ( (context->openCount == 2) || (context->error != NULL) ) {
            break;
        }
    } while (true);
    
    CFRunLoopTimerInvalidate(timer);
    CFRelease(timer);
}

/*! Extracts and returns the socket from a stream.  Reconfigures the stream so that
 *  closing the stream doesn't close the socket.
 *  \param readStream The stream from which to extract the socket.
 *  \param sockPtr A pointer to a socket; the pointer itself must not be NULL;
 *  on entry, the pointed-to value is ignored; on success, it's set to the newly-created
 *  socket; on error the value is unchanged.
 *  \returns On success, 0; on error, an errno-style error number.
 */

static errno_t ExtractSocketFromReadStream(CFReadStreamRef readStream, int * sockPtr) {
    errno_t        err;
    Boolean        success;
    CFDataRef      sockData;
    
    err = EINVAL;
    
    // None of this should fail but, if it does, we've set things up so that we leak
    // the socket rather double close the socket.  That is, we set
    // kCFStreamPropertyShouldCloseNativeSocket to false before getting
    // kCFStreamPropertySocketNativeHandle.
    
    success = CFReadStreamSetProperty(readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanFalse);
    if (success) {
        sockData = CFReadStreamCopyProperty(readStream, kCFStreamPropertySocketNativeHandle);
        if (sockData != NULL) {
            if (CFDataGetLength(sockData) == sizeof(int)) {
                *sockPtr = * (const int *) CFDataGetBytePtr(sockData);
                assert(*sockPtr >= 0);
                err = 0;
            }
            CFRelease(sockData);
        }
    }
    return err;
}

/*! Destroys the supplied read and write streams.
 *  \param readStream The read stream to destroy.
 *  \param writeStream The read stream to destroy.
 */

static void DestroyStreams(CFReadStreamRef readStream, CFWriteStreamRef writeStream) {
    Boolean    success;
    
    assert( readStream != NULL);
    assert(writeStream != NULL);
    
    success = CFReadStreamSetClient(  readStream, 0, NULL, NULL);
    assert(success);
    success = CFWriteStreamSetClient(writeStream, 0, NULL, NULL);
    assert(success);
    CFReadStreamClose(  readStream);
    CFWriteStreamClose(writeStream);
    CFRelease( readStream);
    CFRelease(writeStream);
}

/*! Creates a socket that's connect to the specified port on the specified host.
 *  An internal version of SocketConnectedToHostname that takes various parameters
 *  in some easier-to-digest formats.
 *  \param hostName The host to connect to.
 *  \param port The port on that host.
 *  \param deadline When a failed connection should time out.
 *  \param sockPtr A pointer to a socket; the pointer itself must not be NULL;
 *  on entry, the pointed-to value is ignored; on success, it's set to the newly-created
 *  socket; on error the value is unchanged.
 *  \returns On success, 0; on error, an errno-style error number.
 */

static errno_t SocketConnectedToHostnameInternal(
                                                 CFStringRef            hostName,
                                                 int                    port,
                                                 CFAbsoluteTime          deadline,
                                                 int *                  sockPtr
                                                 ) {
    errno_t                err;
    int                    sock;
    CFReadStreamRef        readStream;
    CFWriteStreamRef        writeStream;
    SuperConnectContext    context = { kSuperConnectContextMagic, 0, NULL };
    
    assert(hostName != nil);
    assert(port > 0);
    assert(port < 65536);
    // It's hard to come up with meaningful asserts for deadline.
    assert(sockPtr != NULL);
    
    sock = -1;
    
    err = CreateAndOpenStreams(&context, hostName, port, &readStream, &writeStream);
    if (err == 0) {
        RunRunLoopWithDeadline(&context, deadline);
        
        if (context.openCount == 2) {
            err = ExtractSocketFromReadStream(readStream, &sock);
        } else if (context.error != NULL) {
            if ( CFEqual(CFErrorGetDomain(context.error), kCFErrorDomainPOSIX) ) {
                err = (errno_t) CFErrorGetCode(context.error);
                assert(err != 0);
            } else {
                err = EINVAL;
            }
            CFRelease(context.error);
        } else {
            assert(false);
        }
    }
    DestroyStreams(readStream, writeStream);
    
    if (err == 0) {
        *sockPtr = sock;
    }
    
    return err;
}

/*! Returns a socket that's connected to the specified port on the specified host.
 *  \param hostName The host to connect to.  This can be an IPv4 or IPv6 address, but
 *  it's typically a DNS name.
 *  \param serviceName The name of the service on that host to connect to.  It's common
 *  to use a number here ("631") but it's also possible to supply a service name ("ipp").
 *  \param timeout A timeout; pass NULL for no timeout.
 *  \returns On success, this returns a socket.  On error, this returns -1 and the
 *  error code is in errno.
 */

extern int SocketConnectedToHostname(
                                     const char *            hostName,
                                     const char *            serviceName,
                                     const struct timeval *  timeout
                                     ) {
    int                result;
    errno_t            err;
    CFStringRef        hostStr;
    int                port;
    int                sock;
    CFAbsoluteTime      deadline;
    
    assert(hostName != NULL);
    assert(serviceName != NULL);
    // duration may be NULL
    
    err = 0;
    sock = -1;
    
    // Convert input parameters into formats appropriate for CFNetwork.
    
    hostStr = CFStringCreateWithCString(NULL, hostName, kCFStringEncodingUTF8);
    if (hostStr == NULL) {
        err = EINVAL;
    }
    
    if (err == 0) {
        err = PortForService(serviceName, &port);
    }
    
    if (err == 0) {
        if (timeout == NULL) {
            deadline = DBL_MAX;
        } else {
            deadline = CFAbsoluteTimeGetCurrent() + (CFAbsoluteTime) timeout->tv_sec + ((CFAbsoluteTime) timeout->tv_usec) / 1000000.0;
        }
    }
    
    // Do the work.
    
    if (err == 0) {
        err = SocketConnectedToHostnameInternal(hostStr, port, deadline, &sock);
    }
    
    // Clean up.
    
    if (hostStr != NULL) {
        CFRelease(hostStr);
    }
    if (err == 0) {
        result = sock;
        assert(result >= 0);
    } else {
        result = -1;
        errno = err;
    }
    
    return result;
} 
