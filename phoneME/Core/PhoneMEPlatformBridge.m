#import <UIKit/UIKit.h>

#include <stdint.h>

static NSURL *PhoneMEPlatformURL(NSString *value) {
    if (value.length == 0) {
        return nil;
    }

    NSURL *url = [NSURL URLWithString:value];
    if (url.scheme.length != 0) {
        return url;
    }

    NSString *escaped = [value stringByAddingPercentEncodingWithAllowedCharacters:
        [NSCharacterSet URLFragmentAllowedCharacterSet]];
    url = escaped == nil ? nil : [NSURL URLWithString:escaped];
    return url.scheme.length == 0 ? nil : url;
}

int32_t phoneme_ios_platform_request(const char *url_bytes) {
    if (url_bytes == NULL || url_bytes[0] == '\0') {
        return 0;
    }

    NSString *value = [NSString stringWithUTF8String:url_bytes];
    NSURL *url = PhoneMEPlatformURL(value);
    if (url == nil) {
        return 0;
    }

    __block BOOL canOpen = NO;
    void (^openRequest)(void) = ^{
        UIApplication *application = UIApplication.sharedApplication;
        canOpen = [application canOpenURL:url];
        if (canOpen) {
            [application openURL:url
                         options:@{}
               completionHandler:nil];
        }
    };

    if (NSThread.isMainThread) {
        openRequest();
    } else {
        dispatch_sync(dispatch_get_main_queue(), openRequest);
    }

    return canOpen ? 1 : 0;
}
