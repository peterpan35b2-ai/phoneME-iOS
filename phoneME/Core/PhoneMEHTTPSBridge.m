#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <Security/SecProtocolTypes.h>

#include <stdint.h>
#include <string.h>

static const NSUInteger kPhoneMEHTTPSMaximumResponseSize = 64U * 1024U * 1024U;

@interface PhoneMEHTTPSResult : NSObject
@property(nonatomic) NSInteger statusCode;
@property(nonatomic, copy) NSString *responseMessage;
@property(nonatomic, copy) NSString *responseHeaders;
@property(nonatomic, copy) NSString *finalURL;
@property(nonatomic, copy) NSString *TLSProtocol;
@property(nonatomic, copy) NSString *TLSVersion;
@property(nonatomic, copy) NSString *cipherSuite;
@property(nonatomic, copy) NSString *certificateSubject;
@property(nonatomic, copy) NSString *certificateIssuer;
@property(nonatomic, copy) NSString *certificateSerial;
@property(nonatomic) int64_t certificateNotBefore;
@property(nonatomic) int64_t certificateNotAfter;
@property(nonatomic, copy) NSString *errorMessage;
@property(nonatomic, copy) NSData *body;
@end

@implementation PhoneMEHTTPSResult
@end

@interface PhoneMEHTTPSCapture : NSObject <NSURLSessionDelegate, NSURLSessionTaskDelegate>
@property(nonatomic, copy) NSString *TLSProtocol;
@property(nonatomic, copy) NSString *TLSVersion;
@property(nonatomic, copy) NSString *cipherSuite;
@property(nonatomic, copy) NSString *certificateSubject;
@end

static NSString *PhoneMETLSVersionString(NSNumber *value) {
    if (value == nil) {
        return nil;
    }
    switch ((tls_protocol_version_t)value.unsignedShortValue) {
        case 0x0301:
            return @"1.0";
        case 0x0302:
            return @"1.1";
        case tls_protocol_version_TLSv12:
            return @"1.2";
        case tls_protocol_version_TLSv13:
            return @"1.3";
        default:
            return [NSString stringWithFormat:@"0x%04X",
                    value.unsignedShortValue];
    }
}

static NSString *PhoneMECipherSuiteString(NSNumber *value) {
    if (value == nil) {
        return nil;
    }
    switch ((tls_ciphersuite_t)value.unsignedShortValue) {
        case tls_ciphersuite_RSA_WITH_AES_128_CBC_SHA:
            return @"TLS_RSA_WITH_AES_128_CBC_SHA";
        case tls_ciphersuite_RSA_WITH_AES_256_CBC_SHA:
            return @"TLS_RSA_WITH_AES_256_CBC_SHA";
        case tls_ciphersuite_RSA_WITH_AES_128_GCM_SHA256:
            return @"TLS_RSA_WITH_AES_128_GCM_SHA256";
        case tls_ciphersuite_RSA_WITH_AES_256_GCM_SHA384:
            return @"TLS_RSA_WITH_AES_256_GCM_SHA384";
        case tls_ciphersuite_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
            return @"TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
        case tls_ciphersuite_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
            return @"TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
        case tls_ciphersuite_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
            return @"TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
        case tls_ciphersuite_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
            return @"TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
        case tls_ciphersuite_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256:
            return @"TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256";
        case tls_ciphersuite_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256:
            return @"TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256";
        case tls_ciphersuite_AES_128_GCM_SHA256:
            return @"TLS_AES_128_GCM_SHA256";
        case tls_ciphersuite_AES_256_GCM_SHA384:
            return @"TLS_AES_256_GCM_SHA384";
        case tls_ciphersuite_CHACHA20_POLY1305_SHA256:
            return @"TLS_CHACHA20_POLY1305_SHA256";
        default:
            return [NSString stringWithFormat:@"TLS_CIPHER_0x%04X",
                    value.unsignedShortValue];
    }
}

@implementation PhoneMEHTTPSCapture

- (void)URLSession:(NSURLSession *)session
        didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
          completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition disposition,
                                      NSURLCredential *credential))completionHandler {
    if ([challenge.protectionSpace.authenticationMethod
            isEqualToString:NSURLAuthenticationMethodServerTrust]) {
        SecTrustRef trust = challenge.protectionSpace.serverTrust;
        if (trust != NULL) {
            CFArrayRef chain = SecTrustCopyCertificateChain(trust);
            if (chain != NULL && CFArrayGetCount(chain) > 0) {
                SecCertificateRef certificate =
                    (SecCertificateRef)CFArrayGetValueAtIndex(chain, 0);
                if (certificate != NULL) {
                    CFStringRef summary =
                        SecCertificateCopySubjectSummary(certificate);
                    if (summary != NULL) {
                        NSString *subject = CFBridgingRelease(summary);
                        if (subject.length != 0) {
                            self.certificateSubject =
                                [subject hasPrefix:@"CN="]
                                    ? subject
                                    : [@"CN=" stringByAppendingString:subject];
                        }
                    }
                }
            }
            if (chain != NULL) {
                CFRelease(chain);
            }
        }
    }
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
 didFinishCollectingMetrics:(NSURLSessionTaskMetrics *)metrics {
    NSURLSessionTaskTransactionMetrics *transaction =
        metrics.transactionMetrics.lastObject;
    if (transaction == nil) {
        return;
    }
    self.TLSProtocol = @"TLS";
    self.TLSVersion =
        PhoneMETLSVersionString(transaction.negotiatedTLSProtocolVersion);
    self.cipherSuite =
        PhoneMECipherSuiteString(transaction.negotiatedTLSCipherSuite);
}

@end

static NSMutableDictionary<NSNumber *, PhoneMEHTTPSResult *> *
PhoneMEHTTPSResults(void) {
    static NSMutableDictionary<NSNumber *, PhoneMEHTTPSResult *> *results;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        results = [[NSMutableDictionary alloc] init];
    });
    return results;
}

static int32_t PhoneMENextHTTPSHandle(void) {
    static int32_t nextHandle = 1;
    @synchronized (PhoneMEHTTPSResults()) {
        if (nextHandle <= 0) {
            nextHandle = 1;
        }
        while (PhoneMEHTTPSResults()[@(nextHandle)] != nil) {
            nextHandle++;
            if (nextHandle <= 0) {
                nextHandle = 1;
            }
        }
        return nextHandle++;
    }
}

static PhoneMEHTTPSResult *PhoneMEHTTPSResultForHandle(int32_t handle) {
    if (handle <= 0) {
        return nil;
    }
    @synchronized (PhoneMEHTTPSResults()) {
        return PhoneMEHTTPSResults()[@(handle)];
    }
}

static NSString *PhoneMEHTTPReasonPhrase(NSInteger statusCode) {
    switch (statusCode) {
        case 100: return @"Continue";
        case 101: return @"Switching Protocols";
        case 200: return @"OK";
        case 201: return @"Created";
        case 202: return @"Accepted";
        case 204: return @"No Content";
        case 206: return @"Partial Content";
        case 300: return @"Multiple Choices";
        case 301: return @"Moved Permanently";
        case 302: return @"Found";
        case 303: return @"See Other";
        case 304: return @"Not Modified";
        case 307: return @"Temporary Redirect";
        case 308: return @"Permanent Redirect";
        case 400: return @"Bad Request";
        case 401: return @"Unauthorized";
        case 403: return @"Forbidden";
        case 404: return @"Not Found";
        case 405: return @"Method Not Allowed";
        case 408: return @"Request Timeout";
        case 409: return @"Conflict";
        case 410: return @"Gone";
        case 411: return @"Length Required";
        case 413: return @"Content Too Large";
        case 414: return @"URI Too Long";
        case 415: return @"Unsupported Media Type";
        case 416: return @"Range Not Satisfiable";
        case 429: return @"Too Many Requests";
        case 500: return @"Internal Server Error";
        case 501: return @"Not Implemented";
        case 502: return @"Bad Gateway";
        case 503: return @"Service Unavailable";
        case 504: return @"Gateway Timeout";
        default:
            return [NSHTTPURLResponse localizedStringForStatusCode:statusCode];
    }
}

static NSString *PhoneMESanitizedHeaderValue(id value) {
    NSString *text = [value isKindOfClass:[NSString class]]
        ? (NSString *)value
        : [value description];
    text = [text stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
    return [text stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
}

static NSString *PhoneMESerializeHeaders(NSDictionary *headers) {
    if (headers.count == 0) {
        return @"";
    }
    NSArray *keys = [[headers allKeys]
        sortedArrayUsingComparator:^NSComparisonResult(id left, id right) {
            return [[left description] caseInsensitiveCompare:[right description]];
        }];
    NSMutableString *serialized = [[NSMutableString alloc] init];
    for (id keyObject in keys) {
        NSString *key = PhoneMESanitizedHeaderValue(keyObject);
        NSString *value = PhoneMESanitizedHeaderValue(headers[keyObject]);
        if (key.length == 0 || value == nil) {
            continue;
        }
        [serialized appendString:key];
        [serialized appendString:@": "];
        [serialized appendString:value];
        [serialized appendString:@"\r\n"];
    }
    return serialized;
}

static void PhoneMEApplyHeaders(NSString *serialized,
                                NSMutableURLRequest *request) {
    NSString *previousKey = nil;
    for (NSString *rawLine in [serialized componentsSeparatedByString:@"\n"]) {
        NSString *line = [rawLine hasSuffix:@"\r"]
            ? [rawLine substringToIndex:rawLine.length - 1]
            : rawLine;
        if (line.length == 0) {
            continue;
        }
        if (([line hasPrefix:@" "] || [line hasPrefix:@"\t"]) &&
                previousKey != nil) {
            NSString *existing = [request valueForHTTPHeaderField:previousKey]
                ?: @"";
            NSString *continued = [line stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceCharacterSet]];
            [request setValue:[NSString stringWithFormat:@"%@ %@",
                               existing, continued]
                forHTTPHeaderField:previousKey];
            continue;
        }

        NSRange separator = [line rangeOfString:@":"];
        if (separator.location == NSNotFound || separator.location == 0) {
            previousKey = nil;
            continue;
        }
        NSString *key = [[line substringToIndex:separator.location]
            stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceCharacterSet]];
        NSString *value = [[line substringFromIndex:separator.location + 1]
            stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceCharacterSet]];
        if (key.length == 0) {
            previousKey = nil;
            continue;
        }

        if ([key caseInsensitiveCompare:@"Host"] == NSOrderedSame ||
            [key caseInsensitiveCompare:@"Content-Length"] == NSOrderedSame ||
            [key caseInsensitiveCompare:@"Connection"] == NSOrderedSame) {
            previousKey = nil;
            continue;
        }
        [request setValue:value forHTTPHeaderField:key];
        previousKey = key;
    }
}

int32_t phoneme_ios_https_execute(const char *url_bytes,
                                  const char *method_bytes,
                                  const char *header_bytes,
                                  const uint8_t *body_bytes,
                                  int32_t body_length,
                                  int32_t timeout_ms) {
    PhoneMEHTTPSResult *result = [[PhoneMEHTTPSResult alloc] init];
    result.body = [NSData data];
    result.responseHeaders = @"";
    result.TLSProtocol = @"TLS";
    result.TLSVersion = @"1.2";
    result.cipherSuite = @"IOS_SYSTEM_CIPHER";
    result.certificateIssuer = @"iOS Trust Store";

    NSString *urlString = url_bytes == NULL
        ? nil
        : [NSString stringWithUTF8String:url_bytes];
    NSString *method = method_bytes == NULL
        ? nil
        : [NSString stringWithUTF8String:method_bytes];
    NSString *headers = header_bytes == NULL
        ? @""
        : [NSString stringWithUTF8String:header_bytes];
    NSURL *url = urlString == nil ? nil : [NSURL URLWithString:urlString];

    if (url == nil || ![url.scheme.lowercaseString isEqualToString:@"https"] ||
            url.host.length == 0 || method.length == 0 || headers == nil ||
            body_length < 0 || (body_length > 0 && body_bytes == NULL)) {
        result.errorMessage = @"Invalid HTTPS request";
    } else {
        NSTimeInterval timeout = timeout_ms > 0
            ? MAX(1.0, (NSTimeInterval)timeout_ms / 1000.0)
            : 60.0;
        NSMutableURLRequest *request =
            [NSMutableURLRequest requestWithURL:url
                                   cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                               timeoutInterval:timeout];
        request.HTTPMethod = method;
        PhoneMEApplyHeaders(headers, request);
        if (body_length > 0) {
            request.HTTPBody = [NSData dataWithBytes:body_bytes
                                              length:(NSUInteger)body_length];
        }

        NSURLSessionConfiguration *configuration =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.requestCachePolicy =
            NSURLRequestReloadIgnoringLocalCacheData;
        configuration.timeoutIntervalForRequest = timeout;
        configuration.timeoutIntervalForResource = timeout;
        configuration.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyAlways;
        configuration.HTTPShouldSetCookies = YES;
        configuration.waitsForConnectivity = NO;

        PhoneMEHTTPSCapture *capture = [[PhoneMEHTTPSCapture alloc] init];
        NSOperationQueue *delegateQueue = [[NSOperationQueue alloc] init];
        delegateQueue.maxConcurrentOperationCount = 1;
        delegateQueue.qualityOfService = NSQualityOfServiceUtility;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration
                                                              delegate:capture
                                                         delegateQueue:delegateQueue];

        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block NSData *responseData = nil;
        __block NSURLResponse *response = nil;
        __block NSError *requestError = nil;
        NSURLSessionDataTask *task = [session dataTaskWithRequest:request
            completionHandler:^(NSData *data, NSURLResponse *taskResponse,
                                NSError *error) {
                responseData = data;
                response = taskResponse;
                requestError = error;
                dispatch_semaphore_signal(semaphore);
            }];
        [task resume];

        int64_t waitNanoseconds =
            (int64_t)((timeout + 2.0) * (double)NSEC_PER_SEC);
        long waitResult = dispatch_semaphore_wait(
            semaphore, dispatch_time(DISPATCH_TIME_NOW, waitNanoseconds));
        if (waitResult != 0) {
            [task cancel];
            result.errorMessage = @"HTTPS request timed out";
        } else if (requestError != nil) {
            result.errorMessage = requestError.localizedDescription
                ?: @"HTTPS request failed";
        } else if (![response isKindOfClass:[NSHTTPURLResponse class]]) {
            result.errorMessage = @"HTTPS server returned an invalid response";
        } else if (responseData.length > kPhoneMEHTTPSMaximumResponseSize) {
            result.errorMessage = @"HTTPS response exceeds 64 MB limit";
        } else {
            NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse *)response;
            result.statusCode = httpResponse.statusCode;
            result.responseMessage =
                PhoneMEHTTPReasonPhrase(httpResponse.statusCode);
            result.responseHeaders =
                PhoneMESerializeHeaders(httpResponse.allHeaderFields);
            result.finalURL = httpResponse.URL.absoluteString ?: urlString;
            result.body = responseData ?: [NSData data];
            result.TLSProtocol = capture.TLSProtocol ?: @"TLS";
            result.TLSVersion = capture.TLSVersion ?: @"1.2";
            result.cipherSuite = capture.cipherSuite ?: @"IOS_SYSTEM_CIPHER";
            result.certificateSubject = capture.certificateSubject
                ?: [@"CN=" stringByAppendingString:url.host];
        }
        [session finishTasksAndInvalidate];
    }

    int32_t handle = PhoneMENextHTTPSHandle();
    @synchronized (PhoneMEHTTPSResults()) {
        PhoneMEHTTPSResults()[@(handle)] = result;
    }
    return handle;
}

int32_t phoneme_ios_https_get_status_code(int32_t handle) {
    PhoneMEHTTPSResult *result = PhoneMEHTTPSResultForHandle(handle);
    return result == nil ? -1 : (int32_t)result.statusCode;
}

int32_t phoneme_ios_https_copy_string(int32_t handle,
                                      int32_t field,
                                      char *destination,
                                      int32_t capacity) {
    PhoneMEHTTPSResult *result = PhoneMEHTTPSResultForHandle(handle);
    if (result == nil) {
        return -1;
    }

    NSString *value = nil;
    switch (field) {
        case 1:
            value = result.responseMessage;
            break;
        case 2:
            value = result.responseHeaders;
            break;
        case 3:
            value = result.finalURL;
            break;
        case 4:
            value = result.TLSProtocol;
            break;
        case 5:
            value = result.TLSVersion;
            break;
        case 6:
            value = result.cipherSuite;
            break;
        case 7:
            value = result.certificateSubject;
            break;
        case 8:
            value = result.certificateIssuer;
            break;
        case 9:
            value = result.certificateSerial;
            break;
        case 10:
            value = result.errorMessage;
            break;
        default:
            return -1;
    }
    if (value == nil) {
        return -1;
    }

    NSData *utf8 = [value dataUsingEncoding:NSUTF8StringEncoding];
    if (utf8 == nil || utf8.length > INT32_MAX) {
        return -1;
    }
    int32_t length = (int32_t)utf8.length;
    if (destination != NULL && capacity > 0) {
        if (capacity <= length) {
            return -1;
        }
        if (length > 0) {
            memcpy(destination, utf8.bytes, (size_t)length);
        }
        destination[length] = '\0';
    }
    return length;
}

int32_t phoneme_ios_https_copy_body(int32_t handle,
                                    uint8_t *destination,
                                    int32_t capacity) {
    PhoneMEHTTPSResult *result = PhoneMEHTTPSResultForHandle(handle);
    if (result == nil || result.body.length > INT32_MAX) {
        return -1;
    }
    int32_t length = (int32_t)result.body.length;
    if (destination != NULL) {
        if (capacity < length) {
            return -1;
        }
        if (length > 0) {
            memcpy(destination, result.body.bytes, (size_t)length);
        }
    }
    return length;
}

int64_t phoneme_ios_https_get_long(int32_t handle, int32_t field) {
    PhoneMEHTTPSResult *result = PhoneMEHTTPSResultForHandle(handle);
    if (result == nil) {
        return 0;
    }
    switch (field) {
        case 1:
            return result.certificateNotBefore;
        case 2:
            return result.certificateNotAfter;
        default:
            return 0;
    }
}

void phoneme_ios_https_close(int32_t handle) {
    if (handle <= 0) {
        return;
    }
    @synchronized (PhoneMEHTTPSResults()) {
        [PhoneMEHTTPSResults() removeObjectForKey:@(handle)];
    }
}

void phoneme_ios_https_reset(void) {
    @synchronized (PhoneMEHTTPSResults()) {
        [PhoneMEHTTPSResults() removeAllObjects];
    }
}
