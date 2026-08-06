#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <Security/SecProtocolTypes.h>

#include <float.h>
#include <stdint.h>
#include <string.h>

static const NSUInteger kPhoneMEHTTPSDefaultMaximumResponseSize =
    64U * 1024U * 1024U;
static const NSTimeInterval kPhoneMEHTTPSNoTimeoutInterval = DBL_MAX;

#if defined(PHONEME_HTTPS_TESTING)
static NSUInteger gPhoneMEHTTPSMaximumResponseSize =
    kPhoneMEHTTPSDefaultMaximumResponseSize;
static NSTimeInterval gPhoneMEHTTPSLastTimeoutInterval = 0.0;
static BOOL gPhoneMEHTTPSLastWaitsForConnectivity = NO;
void phoneme_ios_https_set_test_response_limit(int32_t bytes) {
    gPhoneMEHTTPSMaximumResponseSize = bytes > 0
        ? (NSUInteger)bytes : kPhoneMEHTTPSDefaultMaximumResponseSize;
}
double phoneme_ios_https_get_test_timeout_interval(void) {
    return gPhoneMEHTTPSLastTimeoutInterval;
}
int32_t phoneme_ios_https_get_test_waits_for_connectivity(void) {
    return gPhoneMEHTTPSLastWaitsForConnectivity ? 1 : 0;
}
static NSUInteger PhoneMEHTTPSMaximumResponseSize(void) {
    return gPhoneMEHTTPSMaximumResponseSize;
}
#else
static NSUInteger PhoneMEHTTPSMaximumResponseSize(void) {
    return kPhoneMEHTTPSDefaultMaximumResponseSize;
}
#endif

static NSString *PhoneMEHTTPSResponseLimitError(NSUInteger limit) {
    const NSUInteger megabyte = 1024U * 1024U;
    if (limit != 0U && limit % megabyte == 0U) {
        return [NSString stringWithFormat:
            @"HTTP response exceeds %llu MB limit",
            (unsigned long long)(limit / megabyte)];
    }
    return [NSString stringWithFormat:
        @"HTTP response exceeds %llu byte limit",
        (unsigned long long)limit];
}

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
@property(nonatomic) NSInteger errorCode;
@property(nonatomic, strong) NSData *body;
@end

@implementation PhoneMEHTTPSResult
@end

typedef void (*PhoneMEHTTPSCompletion)(int32_t handle, void *context);

@interface PhoneMEHTTPSPending : NSObject
@property(nonatomic, strong) NSURLSession *session;
@property(nonatomic, strong) NSURLSessionDataTask *task;
@property(nonatomic, assign) PhoneMEHTTPSCompletion completion;
@property(nonatomic, assign) void *context;
@property(nonatomic, assign) int64_t cookieSessionID;
@end

@implementation PhoneMEHTTPSPending
@end

@interface PhoneMEHTTPSCapture : NSObject <NSURLSessionDelegate, NSURLSessionTaskDelegate, NSURLSessionDataDelegate>
@property(nonatomic, copy) NSString *TLSProtocol;
@property(nonatomic, copy) NSString *TLSVersion;
@property(nonatomic, copy) NSString *cipherSuite;
@property(nonatomic, copy) NSString *certificateSubject;
@property(nonatomic, copy) NSString *certificateIssuer;
@property(nonatomic, copy) NSString *certificateSerial;
@property(nonatomic) int64_t certificateNotBefore;
@property(nonatomic) int64_t certificateNotAfter;
@property(nonatomic, copy) NSString *allowedRedirectScheme;
@property(nonatomic, assign) NSInteger redirectLimit;
@property(nonatomic, assign) NSInteger redirectsFollowed;
@property(nonatomic, assign) int32_t handle;
@property(nonatomic, strong) PhoneMEHTTPSResult *result;
@property(nonatomic, strong) NSMutableData *responseBody;
@property(nonatomic, assign) BOOL secureRequest;
@property(nonatomic, assign) BOOL responseTooLarge;
@property(nonatomic, copy) NSString *fallbackCertificateHost;
@property(nonatomic, assign) int64_t cookieSessionID;
@property(nonatomic, copy) NSString *explicitCookieHeader;
@end

static NSMutableDictionary<NSNumber *, PhoneMEHTTPSResult *> *
PhoneMEHTTPSResults(void);
static NSMutableDictionary<NSNumber *, PhoneMEHTTPSPending *> *
PhoneMEHTTPSPendingRequests(void);
static NSMutableDictionary<NSNumber *, NSMutableArray<NSHTTPCookie *> *> *
PhoneMEHTTPSCookieJars(void);
static NSMutableSet<NSNumber *> *PhoneMEHTTPSRevokedCookieSessions(void);
static NSMutableDictionary<NSNumber *, NSOperationQueue *> *
PhoneMEHTTPSDelegateQueues(void);
static NSOperationQueue *PhoneMEHTTPSDelegateQueue(int64_t sessionID);
static NSURLSession *PhoneMETranslationSession(void);
static void PhoneMEStoreResponseCookies(int64_t sessionID,
                                        NSHTTPURLResponse *response);
static void PhoneMEApplyStoredCookies(int64_t sessionID,
                                      NSMutableURLRequest *request);
static NSString *PhoneMEHTTPReasonPhrase(NSInteger statusCode);
static NSString *PhoneMESerializeHeaders(NSDictionary *headers);

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

static NSString *PhoneMECertificateName(SecCertificateRef certificate) {
    if (certificate == NULL) {
        return nil;
    }
    CFStringRef summary = SecCertificateCopySubjectSummary(certificate);
    if (summary == NULL) {
        return nil;
    }
    NSString *name = CFBridgingRelease(summary);
    if (name.length == 0) {
        return nil;
    }
    return [name hasPrefix:@"CN="] ? name
                                   : [@"CN=" stringByAppendingString:name];
}

static NSString *PhoneMEHexString(NSData *data) {
    if (data.length == 0) {
        return nil;
    }
    const uint8_t *bytes = data.bytes;
    NSMutableString *result =
        [[NSMutableString alloc] initWithCapacity:data.length * 2U];
    for (NSUInteger index = 0; index < data.length; ++index) {
        [result appendFormat:@"%02X", bytes[index]];
    }
    return result;
}

static NSString *PhoneMECertificateSerial(SecCertificateRef certificate) {
    if (certificate == NULL) {
        return nil;
    }
    CFErrorRef error = NULL;
    CFDataRef serial = SecCertificateCopySerialNumberData(certificate, &error);
    if (error != NULL) {
        CFRelease(error);
    }
    if (serial == NULL) {
        return nil;
    }
    NSData *data = CFBridgingRelease(serial);
    return PhoneMEHexString(data);
}

typedef struct {
    const uint8_t *bytes;
    NSUInteger length;
    NSUInteger cursor;
} PhoneMEDERCursor;

static BOOL PhoneMEDERReadLength(PhoneMEDERCursor *cursor,
                                 NSUInteger *value) {
    if (cursor == NULL || value == NULL || cursor->cursor >= cursor->length) {
        return NO;
    }
    const uint8_t first = cursor->bytes[cursor->cursor++];
    if ((first & 0x80U) == 0U) {
        *value = first;
        return *value <= cursor->length - cursor->cursor;
    }

    const NSUInteger byteCount = (NSUInteger)(first & 0x7FU);
    if (byteCount == 0U || byteCount > sizeof(NSUInteger) ||
            byteCount > cursor->length - cursor->cursor) {
        return NO;
    }
    NSUInteger result = 0U;
    for (NSUInteger index = 0U; index < byteCount; ++index) {
        if (result > (NSUIntegerMax >> 8U)) {
            return NO;
        }
        result = (result << 8U) | cursor->bytes[cursor->cursor++];
    }
    if (result > cursor->length - cursor->cursor) {
        return NO;
    }
    *value = result;
    return YES;
}

static BOOL PhoneMEDERReadElement(PhoneMEDERCursor *cursor,
                                  uint8_t *tag,
                                  const uint8_t **contents,
                                  NSUInteger *contentLength) {
    if (cursor == NULL || tag == NULL || contents == NULL ||
            contentLength == NULL || cursor->cursor >= cursor->length) {
        return NO;
    }
    *tag = cursor->bytes[cursor->cursor++];
    if (!PhoneMEDERReadLength(cursor, contentLength)) {
        return NO;
    }
    *contents = cursor->bytes + cursor->cursor;
    cursor->cursor += *contentLength;
    return YES;
}

static BOOL PhoneMEDERSkipElement(PhoneMEDERCursor *cursor) {
    uint8_t tag = 0U;
    const uint8_t *contents = NULL;
    NSUInteger contentLength = 0U;
    return PhoneMEDERReadElement(cursor, &tag, &contents, &contentLength);
}

static int PhoneMEDecimalPair(const uint8_t *bytes) {
    if (bytes == NULL || bytes[0] < '0' || bytes[0] > '9' ||
            bytes[1] < '0' || bytes[1] > '9') {
        return -1;
    }
    return (int)(bytes[0] - '0') * 10 + (int)(bytes[1] - '0');
}

static int64_t PhoneMEASN1TimeMilliseconds(uint8_t tag,
                                           const uint8_t *bytes,
                                           NSUInteger length) {
    const BOOL utcTime = tag == 0x17U;
    const BOOL generalizedTime = tag == 0x18U;
    if ((!utcTime && !generalizedTime) || bytes == NULL ||
            length == 0U || bytes[length - 1U] != 'Z') {
        return 0;
    }

    const NSUInteger expectedLength = utcTime ? 13U : 15U;
    if (length != expectedLength) {
        return 0;
    }

    NSUInteger cursor = 0U;
    int year = 0;
    if (utcTime) {
        const int shortYear = PhoneMEDecimalPair(bytes);
        if (shortYear < 0) return 0;
        year = shortYear >= 50 ? 1900 + shortYear : 2000 + shortYear;
        cursor = 2U;
    } else {
        const int century = PhoneMEDecimalPair(bytes);
        const int shortYear = PhoneMEDecimalPair(bytes + 2U);
        if (century < 0 || shortYear < 0) return 0;
        year = century * 100 + shortYear;
        cursor = 4U;
    }

    const int month = PhoneMEDecimalPair(bytes + cursor);
    const int day = PhoneMEDecimalPair(bytes + cursor + 2U);
    const int hour = PhoneMEDecimalPair(bytes + cursor + 4U);
    const int minute = PhoneMEDecimalPair(bytes + cursor + 6U);
    const int second = PhoneMEDecimalPair(bytes + cursor + 8U);
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
            second < 0 || second > 60) {
        return 0;
    }

    NSDateComponents *components = [[NSDateComponents alloc] init];
    components.calendar =
        [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    components.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
    components.year = year;
    components.month = month;
    components.day = day;
    components.hour = hour;
    components.minute = minute;
    components.second = second < 59 ? second : 59;
    NSDate *date = [components.calendar dateFromComponents:components];
    if (date == nil) {
        return 0;
    }
    return (int64_t)(date.timeIntervalSince1970 * 1000.0);
}

static void PhoneMECertificateValidity(SecCertificateRef certificate,
                                       int64_t *notBefore,
                                       int64_t *notAfter) {
    if (notBefore != NULL) *notBefore = 0;
    if (notAfter != NULL) *notAfter = 0;
    if (certificate == NULL) return;

    CFDataRef copiedData = SecCertificateCopyData(certificate);
    if (copiedData == NULL) return;
    NSData *data = CFBridgingRelease(copiedData);
    PhoneMEDERCursor certificateCursor = {
        .bytes = data.bytes,
        .length = data.length,
        .cursor = 0U,
    };

    uint8_t tag = 0U;
    const uint8_t *certificateContents = NULL;
    NSUInteger certificateLength = 0U;
    if (!PhoneMEDERReadElement(&certificateCursor, &tag,
                               &certificateContents, &certificateLength) ||
            tag != 0x30U || certificateCursor.cursor != certificateCursor.length) {
        return;
    }

    PhoneMEDERCursor outer = {
        .bytes = certificateContents,
        .length = certificateLength,
        .cursor = 0U,
    };
    const uint8_t *tbsContents = NULL;
    NSUInteger tbsLength = 0U;
    if (!PhoneMEDERReadElement(&outer, &tag, &tbsContents, &tbsLength) ||
            tag != 0x30U) {
        return;
    }

    PhoneMEDERCursor tbs = {
        .bytes = tbsContents,
        .length = tbsLength,
        .cursor = 0U,
    };
    if (tbs.cursor < tbs.length && tbs.bytes[tbs.cursor] == 0xA0U &&
            !PhoneMEDERSkipElement(&tbs)) {
        return;
    }
    // serialNumber, signature, issuer
    if (!PhoneMEDERSkipElement(&tbs) || !PhoneMEDERSkipElement(&tbs) ||
            !PhoneMEDERSkipElement(&tbs)) {
        return;
    }

    const uint8_t *validityContents = NULL;
    NSUInteger validityLength = 0U;
    if (!PhoneMEDERReadElement(&tbs, &tag, &validityContents,
                               &validityLength) || tag != 0x30U) {
        return;
    }
    PhoneMEDERCursor validity = {
        .bytes = validityContents,
        .length = validityLength,
        .cursor = 0U,
    };
    uint8_t beforeTag = 0U;
    uint8_t afterTag = 0U;
    const uint8_t *beforeContents = NULL;
    const uint8_t *afterContents = NULL;
    NSUInteger beforeLength = 0U;
    NSUInteger afterLength = 0U;
    if (!PhoneMEDERReadElement(&validity, &beforeTag, &beforeContents,
                               &beforeLength) ||
            !PhoneMEDERReadElement(&validity, &afterTag, &afterContents,
                                   &afterLength) ||
            validity.cursor != validity.length) {
        return;
    }
    if (notBefore != NULL) {
        *notBefore = PhoneMEASN1TimeMilliseconds(
            beforeTag, beforeContents, beforeLength);
    }
    if (notAfter != NULL) {
        *notAfter = PhoneMEASN1TimeMilliseconds(
            afterTag, afterContents, afterLength);
    }
}

static NSInteger PhoneMEEffectiveURLPort(NSURL *url) {
    if (url.port != nil) return url.port.integerValue;
    return [url.scheme.lowercaseString isEqualToString:@"https"] ? 443 : 80;
}

static BOOL PhoneMEURLAuthorityChanged(NSURL *previous, NSURL *next) {
    if (previous == nil || next == nil) return YES;
    NSString *previousHost = previous.host.lowercaseString;
    if (previousHost == nil) previousHost = @"";
    NSString *nextHost = next.host.lowercaseString;
    if (nextHost == nil) nextHost = @"";
    return ![previous.scheme.lowercaseString
                isEqualToString:next.scheme.lowercaseString] ||
           ![previousHost isEqualToString:nextHost] ||
           PhoneMEEffectiveURLPort(previous) != PhoneMEEffectiveURLPort(next);
}

static int64_t PhoneMEDeclaredContentLength(
    NSHTTPURLResponse *response) {
    NSString *value = [response valueForHTTPHeaderField:@"Content-Length"];
    if (value.length == 0) return -1;
    NSScanner *scanner = [NSScanner scannerWithString:value];
    long long parsed = -1;
    if (![scanner scanLongLong:&parsed] || !scanner.isAtEnd || parsed < 0) {
        return -1;
    }
    return (int64_t)parsed;
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
                const CFIndex certificateCount = CFArrayGetCount(chain);
                SecCertificateRef leaf =
                    (SecCertificateRef)CFArrayGetValueAtIndex(chain, 0);
                SecCertificateRef issuer = certificateCount > 1
                    ? (SecCertificateRef)CFArrayGetValueAtIndex(chain, 1)
                    : leaf;
                if (leaf != NULL) {
                    self.certificateSubject = PhoneMECertificateName(leaf);
                    self.certificateIssuer = PhoneMECertificateName(issuer);
                    self.certificateSerial = PhoneMECertificateSerial(leaf);
                    int64_t notBefore = 0;
                    int64_t notAfter = 0;
                    PhoneMECertificateValidity(leaf, &notBefore, &notAfter);
                    self.certificateNotBefore = notBefore;
                    self.certificateNotAfter = notAfter;
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
willPerformHTTPRedirection:(NSHTTPURLResponse *)response
        newRequest:(NSURLRequest *)request
 completionHandler:(void (^)(NSURLRequest *request))completionHandler {
    PhoneMEStoreResponseCookies(self.cookieSessionID, response);
    NSString *redirectScheme = request.URL.scheme.lowercaseString;
    if (redirectScheme.length == 0 ||
            ![redirectScheme isEqualToString:self.allowedRedirectScheme]) {
        completionHandler(nil);
        return;
    }
    if (self.redirectsFollowed >= self.redirectLimit) {
        completionHandler(nil);
        return;
    }
    self.redirectsFollowed += 1;
    NSMutableURLRequest *redirected = [request mutableCopy];
    const BOOL authorityChanged =
        PhoneMEURLAuthorityChanged(response.URL, request.URL);
    if (authorityChanged) {
        [redirected setValue:nil forHTTPHeaderField:@"Authorization"];
        [redirected setValue:nil forHTTPHeaderField:@"Proxy-Authorization"];
    }
    [redirected setValue:nil forHTTPHeaderField:@"Cookie"];
    PhoneMEApplyStoredCookies(self.cookieSessionID, redirected);
    if (!authorityChanged && self.explicitCookieHeader.length != 0) {
        [redirected setValue:self.explicitCookieHeader
          forHTTPHeaderField:@"Cookie"];
    }
    completionHandler(redirected);
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

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
 didReceiveResponse:(NSURLResponse *)response
  completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler {
    if (![response isKindOfClass:[NSHTTPURLResponse class]]) {
        self.result.errorMessage = @"HTTP server returned an invalid response";
        completionHandler(NSURLSessionResponseCancel);
        return;
    }

    NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse *)response;
    PhoneMEStoreResponseCookies(self.cookieSessionID, httpResponse);
    self.result.statusCode = httpResponse.statusCode;
    self.result.responseMessage = PhoneMEHTTPReasonPhrase(httpResponse.statusCode);
    self.result.responseHeaders =
        PhoneMESerializeHeaders(httpResponse.allHeaderFields);
    NSString *finalURL = httpResponse.URL.absoluteString;
    if (finalURL != nil) self.result.finalURL = finalURL;

    const int64_t expectedLength = response.expectedContentLength;
    const int64_t declaredLength =
        PhoneMEDeclaredContentLength(httpResponse);
    const int64_t boundedLength =
        expectedLength > declaredLength ? expectedLength : declaredLength;
    const NSUInteger responseLimit = PhoneMEHTTPSMaximumResponseSize();
    if (boundedLength > 0 &&
            (uint64_t)boundedLength > (uint64_t)responseLimit) {
        self.responseTooLarge = YES;
        self.result.errorMessage =
            PhoneMEHTTPSResponseLimitError(responseLimit);
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data {
    if (self.result.errorMessage != nil || data.length == 0) {
        return;
    }
    const NSUInteger responseLimit = PhoneMEHTTPSMaximumResponseSize();
    if (self.responseBody.length > responseLimit ||
            data.length > responseLimit - self.responseBody.length) {
        self.responseTooLarge = YES;
        self.result.errorMessage =
            PhoneMEHTTPSResponseLimitError(responseLimit);
        [dataTask cancel];
        return;
    }
    [self.responseBody appendData:data];
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
 didCompleteWithError:(NSError *)error {
    const int64_t expectedLength = task.countOfBytesExpectedToReceive;
    const NSUInteger responseLimit = PhoneMEHTTPSMaximumResponseSize();
    if (expectedLength > 0 &&
            (uint64_t)expectedLength > (uint64_t)responseLimit) {
        self.responseTooLarge = YES;
        self.result.errorMessage =
            PhoneMEHTTPSResponseLimitError(responseLimit);
        self.result.errorCode = 0;
        [self.responseBody setLength:0U];
    } else if (error != nil && self.result.errorMessage == nil) {
        NSString *description = error.localizedDescription;
        self.result.errorMessage = description != nil
            ? description : @"HTTP request failed";
        self.result.errorCode =
            [error.domain isEqualToString:NSURLErrorDomain] ? error.code : 0;
    }
    if (self.result.errorMessage == nil && self.result.statusCode < 100) {
        self.result.errorMessage = @"HTTP server returned an invalid response";
    }
    if (self.responseTooLarge) {
        [self.responseBody setLength:0U];
    }

    self.result.body = self.responseBody != nil
        ? self.responseBody : [NSData data];
    self.responseBody = nil;
    if (self.secureRequest && self.result.errorMessage == nil) {
        self.result.TLSProtocol = self.TLSProtocol != nil
            ? self.TLSProtocol : @"TLS";
        self.result.TLSVersion = self.TLSVersion != nil
            ? self.TLSVersion : @"1.2";
        self.result.cipherSuite = self.cipherSuite != nil
            ? self.cipherSuite : @"IOS_SYSTEM_CIPHER";
        self.result.certificateSubject = self.certificateSubject != nil
            ? self.certificateSubject
            : [@"CN=" stringByAppendingString:self.fallbackCertificateHost];
        self.result.certificateIssuer = self.certificateIssuer != nil
            ? self.certificateIssuer : @"";
        self.result.certificateSerial = self.certificateSerial != nil
            ? self.certificateSerial : @"";
        self.result.certificateNotBefore = self.certificateNotBefore;
        self.result.certificateNotAfter = self.certificateNotAfter;
    }

    PhoneMEHTTPSCompletion callback = NULL;
    void *callbackContext = NULL;
    @synchronized (PhoneMEHTTPSResults()) {
        PhoneMEHTTPSPending *active =
            PhoneMEHTTPSPendingRequests()[@(self.handle)];
        if (active != nil) {
            callback = active.completion;
            callbackContext = active.context;
            [PhoneMEHTTPSPendingRequests() removeObjectForKey:@(self.handle)];
        }
        PhoneMEHTTPSResults()[@(self.handle)] = self.result;
    }
    [session finishTasksAndInvalidate];
    if (callback != NULL) {
        callback(self.handle, callbackContext);
    }
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

static NSMutableDictionary<NSNumber *, PhoneMEHTTPSPending *> *
PhoneMEHTTPSPendingRequests(void) {
    static NSMutableDictionary<NSNumber *, PhoneMEHTTPSPending *> *pending;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        pending = [[NSMutableDictionary alloc] init];
    });
    return pending;
}

static NSMutableDictionary<NSNumber *, NSMutableArray<NSHTTPCookie *> *> *
PhoneMEHTTPSCookieJars(void) {
    static NSMutableDictionary<NSNumber *, NSMutableArray<NSHTTPCookie *> *> *jars;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        jars = [[NSMutableDictionary alloc] init];
    });
    return jars;
}

static NSMutableSet<NSNumber *> *PhoneMEHTTPSRevokedCookieSessions(void) {
    static NSMutableSet<NSNumber *> *sessions;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sessions = [[NSMutableSet alloc] init];
    });
    return sessions;
}

static NSMutableDictionary<NSNumber *, NSOperationQueue *> *
PhoneMEHTTPSDelegateQueues(void) {
    static NSMutableDictionary<NSNumber *, NSOperationQueue *> *queues;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        queues = [[NSMutableDictionary alloc] init];
    });
    return queues;
}

static NSOperationQueue *PhoneMEHTTPSDelegateQueue(int64_t sessionID) {
    NSNumber *key = @(sessionID);
    @synchronized (PhoneMEHTTPSResults()) {
        NSOperationQueue *queue = PhoneMEHTTPSDelegateQueues()[key];
        if (queue == nil) {
            queue = [[NSOperationQueue alloc] init];
            queue.maxConcurrentOperationCount = 1;
            queue.qualityOfService = NSQualityOfServiceUtility;
            queue.name = [NSString stringWithFormat:
                @"phoneME.http.delegate.%lld", (long long)sessionID];
            PhoneMEHTTPSDelegateQueues()[key] = queue;
        }
        return queue;
    }
}

static NSURLSession *PhoneMETranslationSession(void) {
    static NSURLSession *session;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSURLSessionConfiguration *configuration =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.requestCachePolicy =
            NSURLRequestReloadIgnoringLocalCacheData;
        configuration.URLCache = nil;
        configuration.HTTPCookieStorage = nil;
        configuration.HTTPShouldSetCookies = NO;
        configuration.waitsForConnectivity = NO;
        configuration.HTTPShouldUsePipelining = YES;
        configuration.HTTPMaximumConnectionsPerHost = 4;
        session = [NSURLSession sessionWithConfiguration:configuration];
    });
    return session;
}

static BOOL PhoneMECookieMatchesURL(NSHTTPCookie *cookie, NSURL *url) {
    if (cookie == nil || url == nil || url.host.length == 0) return NO;
    NSDate *expires = cookie.expiresDate;
    if (expires != nil && [expires timeIntervalSinceNow] <= 0.0) return NO;
    if (cookie.isSecure &&
            ![url.scheme.lowercaseString isEqualToString:@"https"]) {
        return NO;
    }

    NSString *host = url.host.lowercaseString;
    NSString *domain = cookie.domain.lowercaseString;
    if (domain.length == 0) return NO;
    const BOOL acceptsSubdomains = [domain hasPrefix:@"."];
    if (acceptsSubdomains) domain = [domain substringFromIndex:1U];
    BOOL domainMatches = [host isEqualToString:domain];
    if (!domainMatches && acceptsSubdomains && host.length > domain.length &&
            [host hasSuffix:domain]) {
        const NSUInteger separator = host.length - domain.length - 1U;
        domainMatches = [host characterAtIndex:separator] == '.';
    }
    if (!domainMatches) return NO;

    NSString *requestPath = url.path.length == 0 ? @"/" : url.path;
    NSString *cookiePath = cookie.path.length == 0 ? @"/" : cookie.path;
    if ([requestPath isEqualToString:cookiePath]) return YES;
    if (![requestPath hasPrefix:cookiePath]) return NO;
    if ([cookiePath hasSuffix:@"/"]) return YES;
    return requestPath.length > cookiePath.length &&
           [requestPath characterAtIndex:cookiePath.length] == '/';
}

static void PhoneMEStoreResponseCookies(int64_t sessionID,
                                        NSHTTPURLResponse *response) {
    if (sessionID <= 0 || response == nil || response.URL == nil) return;
    NSArray<NSHTTPCookie *> *cookies = [NSHTTPCookie
        cookiesWithResponseHeaderFields:
            (NSDictionary<NSString *, NSString *> *)response.allHeaderFields
        forURL:response.URL];
    if (cookies.count == 0) return;

    NSNumber *key = @(sessionID);
    @synchronized (PhoneMEHTTPSResults()) {
        if ([PhoneMEHTTPSRevokedCookieSessions() containsObject:key]) return;
        NSMutableArray<NSHTTPCookie *> *jar = PhoneMEHTTPSCookieJars()[key];
        if (jar == nil) {
            jar = [[NSMutableArray alloc] init];
            PhoneMEHTTPSCookieJars()[key] = jar;
        }
        for (NSHTTPCookie *cookie in cookies) {
            NSIndexSet *duplicates = [jar indexesOfObjectsPassingTest:
                ^BOOL(NSHTTPCookie *stored, NSUInteger index, BOOL *stop) {
                    (void)index;
                    (void)stop;
                    return [stored.name isEqualToString:cookie.name] &&
                           [stored.domain caseInsensitiveCompare:cookie.domain] ==
                               NSOrderedSame &&
                           [stored.path isEqualToString:cookie.path];
                }];
            if (duplicates.count != 0) [jar removeObjectsAtIndexes:duplicates];
            NSDate *expires = cookie.expiresDate;
            if (expires == nil || [expires timeIntervalSinceNow] > 0.0) {
                [jar addObject:cookie];
            }
        }
        static const NSUInteger kMaximumCookiesPerSession = 256U;
        if (jar.count > kMaximumCookiesPerSession) {
            [jar removeObjectsInRange:NSMakeRange(
                0U, jar.count - kMaximumCookiesPerSession)];
        }
    }
}

static void PhoneMEApplyStoredCookies(int64_t sessionID,
                                      NSMutableURLRequest *request) {
    if (sessionID <= 0 || request == nil || request.URL == nil) return;
    if ([request valueForHTTPHeaderField:@"Cookie"].length != 0) return;
    NSNumber *key = @(sessionID);
    NSMutableArray<NSHTTPCookie *> *matching = [[NSMutableArray alloc] init];
    @synchronized (PhoneMEHTTPSResults()) {
        if ([PhoneMEHTTPSRevokedCookieSessions() containsObject:key]) return;
        NSMutableArray<NSHTTPCookie *> *jar = PhoneMEHTTPSCookieJars()[key];
        if (jar == nil) return;
        NSMutableArray<NSHTTPCookie *> *retained = [[NSMutableArray alloc] init];
        for (NSHTTPCookie *cookie in jar) {
            NSDate *expires = cookie.expiresDate;
            if (expires != nil && [expires timeIntervalSinceNow] <= 0.0) {
                continue;
            }
            [retained addObject:cookie];
            if (PhoneMECookieMatchesURL(cookie, request.URL)) {
                [matching addObject:cookie];
            }
        }
        PhoneMEHTTPSCookieJars()[key] = retained;
    }
    if (matching.count == 0) return;
    NSDictionary<NSString *, NSString *> *fields =
        [NSHTTPCookie requestHeaderFieldsWithCookies:matching];
    NSString *cookieHeader = fields[@"Cookie"];
    if (cookieHeader.length != 0) {
        [request setValue:cookieHeader forHTTPHeaderField:@"Cookie"];
    }
}

static int32_t PhoneMENextHTTPSHandle(void) {
    static int32_t nextHandle = 1;
    @synchronized (PhoneMEHTTPSResults()) {
        if (nextHandle <= 0) {
            nextHandle = 1;
        }
        while (PhoneMEHTTPSResults()[@(nextHandle)] != nil ||
               PhoneMEHTTPSPendingRequests()[@(nextHandle)] != nil) {
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

static BOOL PhoneMEApplyHeaders(NSString *serialized,
                                NSMutableURLRequest *request) {
    BOOL explicitCookie = NO;
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
            NSString *existing =
                [request valueForHTTPHeaderField:previousKey];
            if (existing == nil) existing = @"";
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
            [key caseInsensitiveCompare:@"Connection"] == NSOrderedSame ||
            [key caseInsensitiveCompare:@"Transfer-Encoding"] == NSOrderedSame) {
            previousKey = nil;
            continue;
        }
        [request setValue:value forHTTPHeaderField:key];
        if ([key caseInsensitiveCompare:@"Cookie"] == NSOrderedSame) {
            explicitCookie = YES;
        }
        previousKey = key;
    }
    return explicitCookie;
}

int32_t phoneme_ios_https_execute_async(
    const char *url_bytes,
    const char *method_bytes,
    const char *header_bytes,
    const uint8_t *body_bytes,
    int32_t body_length,
    int32_t timeout_ms,
    int32_t redirect_limit,
    int64_t cookie_session_id,
    PhoneMEHTTPSCompletion completion,
    void *context) {
    const int32_t handle = PhoneMENextHTTPSHandle();
    PhoneMEHTTPSResult *result = [[PhoneMEHTTPSResult alloc] init];
    result.body = [NSData data];
    result.responseHeaders = @"";

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
    NSString *scheme = url.scheme.lowercaseString;
    const BOOL supportedScheme = [scheme isEqualToString:@"http"] ||
                                 [scheme isEqualToString:@"https"];

    if (url == nil || !supportedScheme || url.host.length == 0 ||
            method.length == 0 || headers == nil || body_length < 0 ||
            redirect_limit < 0 ||
            (body_length > 0 && body_bytes == NULL)) {
        result.errorMessage = @"Invalid HTTP request";
        @synchronized (PhoneMEHTTPSResults()) {
            PhoneMEHTTPSResults()[@(handle)] = result;
        }
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            if (completion != NULL) completion(handle, context);
        });
        return handle;
    }

    const BOOL secureRequest = [scheme isEqualToString:@"https"];
    result.finalURL = urlString;
    NSTimeInterval timeout = kPhoneMEHTTPSNoTimeoutInterval;
    if (timeout_ms > 0) {
        timeout = (NSTimeInterval)timeout_ms / 1000.0;
        if (timeout < 1.0) timeout = 1.0;
    }
    NSMutableURLRequest *request =
        [NSMutableURLRequest requestWithURL:url
                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                           timeoutInterval:timeout];
    request.HTTPMethod = method;
    PhoneMEApplyStoredCookies(cookie_session_id, request);
    const BOOL explicitCookie = PhoneMEApplyHeaders(headers, request);
    if (body_length > 0) {
        request.HTTPBody = [NSData dataWithBytes:body_bytes
                                          length:(NSUInteger)body_length];
    }

    const BOOL fastTranslationRequest = secureRequest &&
        [url.host.lowercaseString isEqualToString:@"translate.google.com"] &&
        [[request valueForHTTPHeaderField:@"User-Agent"]
            isEqualToString:@"phoneME-iOS/translation"];
    if (fastTranslationRequest) {
        NSURLSession *session = PhoneMETranslationSession();
        PhoneMEHTTPSPending *pending = [[PhoneMEHTTPSPending alloc] init];
        pending.session = session;
        pending.completion = completion;
        pending.context = context;
        pending.cookieSessionID = cookie_session_id;

        NSURLSessionDataTask *task = [session
            dataTaskWithRequest:request
              completionHandler:^(NSData *data,
                                  NSURLResponse *response,
                                  NSError *error) {
            if (error != nil) {
                result.errorMessage = error.localizedDescription != nil
                    ? error.localizedDescription : @"HTTP request failed";
                result.errorCode =
                    [error.domain isEqualToString:NSURLErrorDomain]
                        ? error.code : 0;
            } else if (![response isKindOfClass:[NSHTTPURLResponse class]]) {
                result.errorMessage =
                    @"HTTP server returned an invalid response";
            } else {
                NSHTTPURLResponse *httpResponse =
                    (NSHTTPURLResponse *)response;
                result.statusCode = httpResponse.statusCode;
                result.responseMessage =
                    PhoneMEHTTPReasonPhrase(httpResponse.statusCode);
                result.responseHeaders =
                    PhoneMESerializeHeaders(httpResponse.allHeaderFields);
                NSString *finalURL = httpResponse.URL.absoluteString;
                if (finalURL != nil) result.finalURL = finalURL;

                const NSUInteger responseLimit =
                    PhoneMEHTTPSMaximumResponseSize();
                if (data.length > responseLimit) {
                    result.errorMessage =
                        PhoneMEHTTPSResponseLimitError(responseLimit);
                    result.body = [NSData data];
                } else {
                    result.body = data != nil ? data : [NSData data];
                    result.TLSProtocol = @"TLS";
                }
            }

            PhoneMEHTTPSCompletion callback = NULL;
            void *callbackContext = NULL;
            @synchronized (PhoneMEHTTPSResults()) {
                PhoneMEHTTPSPending *active =
                    PhoneMEHTTPSPendingRequests()[@(handle)];
                if (active != nil) {
                    callback = active.completion;
                    callbackContext = active.context;
                    [PhoneMEHTTPSPendingRequests()
                        removeObjectForKey:@(handle)];
                }
                PhoneMEHTTPSResults()[@(handle)] = result;
            }
            if (callback != NULL) callback(handle, callbackContext);
        }];
        pending.task = task;
        @synchronized (PhoneMEHTTPSResults()) {
            PhoneMEHTTPSPendingRequests()[@(handle)] = pending;
        }
        [task resume];
        return handle;
    }

    NSURLSessionConfiguration *configuration =
        [NSURLSessionConfiguration ephemeralSessionConfiguration];
    configuration.requestCachePolicy =
        NSURLRequestReloadIgnoringLocalCacheData;
    configuration.timeoutIntervalForRequest = timeout;
    configuration.timeoutIntervalForResource = timeout;
    configuration.HTTPCookieStorage = nil;
    configuration.HTTPShouldSetCookies = NO;
    configuration.waitsForConnectivity = timeout_ms <= 0;
#if defined(PHONEME_HTTPS_TESTING)
    gPhoneMEHTTPSLastTimeoutInterval = timeout;
    gPhoneMEHTTPSLastWaitsForConnectivity =
        configuration.waitsForConnectivity;
#endif

    PhoneMEHTTPSCapture *capture = [[PhoneMEHTTPSCapture alloc] init];
    capture.allowedRedirectScheme = scheme;
    capture.redirectLimit = (NSInteger)redirect_limit;
    capture.redirectsFollowed = 0;
    capture.handle = handle;
    capture.result = result;
    capture.responseBody = [[NSMutableData alloc] init];
    capture.secureRequest = secureRequest;
    capture.fallbackCertificateHost = url.host;
    capture.cookieSessionID = cookie_session_id;
    capture.explicitCookieHeader = explicitCookie
        ? [request valueForHTTPHeaderField:@"Cookie"] : nil;

    NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration
                                                          delegate:capture
                                                     delegateQueue:PhoneMEHTTPSDelegateQueue(cookie_session_id)];
    PhoneMEHTTPSPending *pending = [[PhoneMEHTTPSPending alloc] init];
    pending.session = session;
    pending.completion = completion;
    pending.context = context;
    pending.cookieSessionID = cookie_session_id;

    NSURLSessionDataTask *task = [session dataTaskWithRequest:request];
    pending.task = task;
    @synchronized (PhoneMEHTTPSResults()) {
        PhoneMEHTTPSPendingRequests()[@(handle)] = pending;
    }
    [task resume];
    return handle;
}

void phoneme_ios_https_cancel(int32_t handle) {
    PhoneMEHTTPSPending *pending = nil;
    @synchronized (PhoneMEHTTPSResults()) {
        pending = PhoneMEHTTPSPendingRequests()[@(handle)];
    }
    [pending.task cancel];
}

void phoneme_ios_https_clear_session(int64_t cookie_session_id) {
    if (cookie_session_id <= 0) return;
    NSNumber *key = @(cookie_session_id);
    NSMutableArray<PhoneMEHTTPSPending *> *pending =
        [[NSMutableArray alloc] init];
    @synchronized (PhoneMEHTTPSResults()) {
        [PhoneMEHTTPSCookieJars() removeObjectForKey:key];
        [PhoneMEHTTPSDelegateQueues() removeObjectForKey:key];
        [PhoneMEHTTPSRevokedCookieSessions() addObject:key];
        for (PhoneMEHTTPSPending *request in
                PhoneMEHTTPSPendingRequests().allValues) {
            if (request.cookieSessionID == cookie_session_id) {
                [pending addObject:request];
            }
        }
    }
    for (PhoneMEHTTPSPending *request in pending) {
        [request.task cancel];
    }
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
        case 3:
            return (int64_t)result.errorCode;
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
    NSArray<PhoneMEHTTPSPending *> *pending = nil;
    @synchronized (PhoneMEHTTPSResults()) {
        [PhoneMEHTTPSResults() removeAllObjects];
        [PhoneMEHTTPSCookieJars() removeAllObjects];
        [PhoneMEHTTPSDelegateQueues() removeAllObjects];
        [PhoneMEHTTPSRevokedCookieSessions() removeAllObjects];
        pending = PhoneMEHTTPSPendingRequests().allValues;
        for (PhoneMEHTTPSPending *request in pending) {
            if (request.cookieSessionID > 0) {
                [PhoneMEHTTPSRevokedCookieSessions()
                    addObject:@(request.cookieSessionID)];
            }
        }
    }
    for (PhoneMEHTTPSPending *request in pending) {
        [request.task cancel];
    }
}
