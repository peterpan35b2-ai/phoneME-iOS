#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS || TARGET_OS_TV
#import <UIKit/UIKit.h>
#endif

#include <math.h>
#include <stdint.h>

static dispatch_queue_t PMMediaQueue(void) {
    static dispatch_queue_t queue;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        queue = dispatch_queue_create("dev.phoneme.media", DISPATCH_QUEUE_SERIAL);
    });
    return queue;
}

static NSMutableDictionary<NSNumber *, id> *PMMediaRegistry(void) {
    static NSMutableDictionary<NSNumber *, id> *registry;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        registry = [[NSMutableDictionary alloc] init];
    });
    return registry;
}

static NSMutableSet *PMTonePlayers(void) {
    static NSMutableSet *players;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        players = [[NSMutableSet alloc] init];
    });
    return players;
}

static int32_t gNextMediaHandle = 1;
static void *PMStreamStatusContext = &PMStreamStatusContext;

static void PMConfigureAudioSession(void) {
#if TARGET_OS_IOS || TARGET_OS_TV
    AVAudioSession *session = AVAudioSession.sharedInstance;
    NSError *categoryError = nil;
    [session setCategory:AVAudioSessionCategoryPlayback
                    mode:AVAudioSessionModeDefault
                 options:AVAudioSessionCategoryOptionMixWithOthers
                   error:&categoryError];
    if (categoryError != nil) {
        NSLog(@"phoneME media: unable to configure audio session: %@",
              categoryError.localizedDescription);
    }

    NSError *activationError = nil;
    [session setActive:YES error:&activationError];
    if (activationError != nil) {
        NSLog(@"phoneME media: unable to activate audio session: %@",
              activationError.localizedDescription);
    }
#endif
}

static NSString *PMStringFromUTF8(const char *value) {
    if (value == NULL) {
        return nil;
    }
    return [NSString stringWithUTF8String:value];
}

static NSString *PMNormalizedContentType(NSString *type) {
    NSString *value = [[type componentsSeparatedByString:@";"] firstObject].lowercaseString;
    if ([value isEqualToString:@"audio/mp3"]) return @"audio/mpeg";
    if ([value isEqualToString:@"audio/wav"]) return @"audio/x-wav";
    if ([value isEqualToString:@"audio/x-midi"] ||
        [value isEqualToString:@"audio/sp-midi"]) return @"audio/midi";
    if ([value isEqualToString:@"audio/x-m4a"]) return @"audio/mp4";
    if ([value isEqualToString:@"audio/x-aac"] ||
        [value isEqualToString:@"audio/aacp"] ||
        [value isEqualToString:@"audio/mp4a-latm"]) return @"audio/aac";
    return value;
}

static BOOL PMIsMIDIType(NSString *type) {
    return [PMNormalizedContentType(type) isEqualToString:@"audio/midi"];
}

static NSURL *PMURLFromLocator(NSString *locator) {
    NSString *trimmed = [locator stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (trimmed.length == 0) {
        return nil;
    }

    NSURL *url = [NSURL URLWithString:trimmed];
    if (url.scheme.length > 0) {
        return url;
    }

    NSString *escaped = [trimmed stringByAddingPercentEncodingWithAllowedCharacters:
        NSCharacterSet.URLFragmentAllowedCharacterSet];
    url = [NSURL URLWithString:escaped];
    if (url.scheme.length > 0) {
        return url;
    }
    return [NSURL fileURLWithPath:trimmed];
}

@interface PMMediaEntry : NSObject <AVAudioPlayerDelegate>
@property(nonatomic, strong) AVAudioPlayer *audioPlayer;
@property(nonatomic, strong) AVMIDIPlayer *midiPlayer;
@property(nonatomic, strong) AVPlayer *streamPlayer;
@property(nonatomic, strong) id streamEndObserver;
@property(nonatomic, strong) id streamFailedObserver;
@property(nonatomic) BOOL streamFailed;
@property(nonatomic) BOOL observingStreamStatus;
@property(nonatomic) BOOL hasPendingSeek;
@property(nonatomic) int64_t pendingSeekMicroseconds;
@property(nonatomic) BOOL resumeAfterPendingSeek;
@property(nonatomic) NSUInteger seekGeneration;
@property(nonatomic) int32_t handle;
@property(nonatomic) NSInteger loopCount;
@property(nonatomic) NSInteger midiLoopsRemaining;
@property(nonatomic) BOOL ended;
@property(nonatomic) BOOL muted;
@property(nonatomic) float volume;
@property(nonatomic) NSUInteger playbackGeneration;
@property(nonatomic) BOOL transientTone;
@end

@implementation PMMediaEntry

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        _loopCount = 1;
        _volume = 1.0f;
    }
    return self;
}

- (void)dealloc {
    if (_observingStreamStatus && _streamPlayer.currentItem != nil) {
        [_streamPlayer.currentItem removeObserver:self
                                       forKeyPath:@"status"
                                          context:PMStreamStatusContext];
    }
    if (_streamEndObserver != nil) {
        [NSNotificationCenter.defaultCenter removeObserver:_streamEndObserver];
    }
    if (_streamFailedObserver != nil) {
        [NSNotificationCenter.defaultCenter removeObserver:_streamFailedObserver];
    }
}

- (void)applyVolume {
    float effectiveVolume = self.muted ? 0.0f : self.volume;
    self.audioPlayer.volume = effectiveVolume;
    self.streamPlayer.volume = effectiveVolume;
    self.streamPlayer.muted = self.muted;
}

- (void)configureLoopCount:(NSInteger)count {
    self.loopCount = count;
    self.audioPlayer.numberOfLoops = count == -1 ? -1 : MAX(0, count - 1);
}

- (BOOL)start {
    PMConfigureAudioSession();
    self.ended = NO;
    self.streamFailed = NO;
    self.playbackGeneration += 1;
    NSUInteger generation = self.playbackGeneration;

    if (self.audioPlayer != nil) {
        if (self.audioPlayer.currentTime >= self.audioPlayer.duration &&
            self.audioPlayer.duration > 0) {
            self.audioPlayer.currentTime = 0;
        }
        [self applyVolume];
        return [self.audioPlayer play];
    }

    if (self.midiPlayer != nil) {
        if (self.midiPlayer.currentPosition >= self.midiPlayer.duration &&
            self.midiPlayer.duration > 0) {
            self.midiPlayer.currentPosition = 0;
        }
        self.midiLoopsRemaining = self.loopCount;
        [self applyVolume];
        [self playMIDIGeneration:generation];
        return YES;
    }

    if (self.streamPlayer != nil) {
        AVPlayerItem *item = self.streamPlayer.currentItem;
        if (item.status == AVPlayerItemStatusFailed || self.streamPlayer.error != nil) {
            self.streamFailed = YES;
            return NO;
        }
        if (item != nil && CMTIME_IS_NUMERIC(item.duration) &&
            CMTimeCompare(self.streamPlayer.currentTime, item.duration) >= 0) {
            self.pendingSeekMicroseconds = 0;
            self.hasPendingSeek = YES;
        }
        [self applyVolume];
        if (self.hasPendingSeek) {
            self.resumeAfterPendingSeek = YES;
            [self applyPendingSeekIfPossible];
            return YES;
        }
        [self.streamPlayer play];
        if (item.status == AVPlayerItemStatusFailed || self.streamPlayer.error != nil) {
            self.streamFailed = YES;
            return NO;
        }
        return YES;
    }

    return NO;
}

- (void)playMIDIGeneration:(NSUInteger)generation {
    if (self.midiPlayer == nil || generation != self.playbackGeneration) {
        return;
    }

    __weak PMMediaEntry *weakSelf = self;
    [self.midiPlayer play:^{
        dispatch_async(PMMediaQueue(), ^{
            PMMediaEntry *strongSelf = weakSelf;
            if (strongSelf == nil || generation != strongSelf.playbackGeneration ||
                strongSelf.midiPlayer == nil) {
                return;
            }

            if (strongSelf.loopCount == -1 || strongSelf.midiLoopsRemaining > 1) {
                if (strongSelf.loopCount != -1) {
                    strongSelf.midiLoopsRemaining -= 1;
                }
                strongSelf.midiPlayer.currentPosition = 0;
                [strongSelf playMIDIGeneration:generation];
            } else {
                strongSelf.ended = YES;
            }
        });
    }];
}

- (BOOL)stop {
    self.playbackGeneration += 1;
    if (self.audioPlayer != nil) {
        [self.audioPlayer pause];
        return YES;
    }
    if (self.midiPlayer != nil) {
        [self.midiPlayer stop];
        return YES;
    }
    if (self.streamPlayer != nil) {
        self.resumeAfterPendingSeek = NO;
        [self.streamPlayer pause];
        return YES;
    }
    return NO;
}

- (BOOL)isPlaying {
    if (self.audioPlayer != nil) return self.audioPlayer.isPlaying;
    if (self.midiPlayer != nil) return self.midiPlayer.isPlaying;
    if (self.streamPlayer != nil) {
        if (@available(iOS 10.0, macOS 10.12, tvOS 10.0, *)) {
            return self.streamPlayer.timeControlStatus == AVPlayerTimeControlStatusPlaying;
        }
        return self.streamPlayer.rate != 0.0f;
    }
    return NO;
}

- (BOOL)hasError {
    AVPlayerItem *item = self.streamPlayer.currentItem;
    if (item.status == AVPlayerItemStatusFailed || self.streamPlayer.error != nil) {
        self.streamFailed = YES;
    }
    return self.streamFailed;
}

- (int64_t)mediaTimeMicroseconds {
    NSTimeInterval seconds = 0;
    if (self.audioPlayer != nil) {
        seconds = self.audioPlayer.currentTime;
    } else if (self.midiPlayer != nil) {
        seconds = self.midiPlayer.currentPosition;
    } else if (self.streamPlayer != nil) {
        if (self.hasPendingSeek) {
            return self.pendingSeekMicroseconds;
        }
        CMTime time = self.streamPlayer.currentTime;
        if (CMTIME_IS_NUMERIC(time)) {
            seconds = CMTimeGetSeconds(time);
        }
    }
    if (!isfinite(seconds) || seconds < 0) seconds = 0;
    return (int64_t)llround(seconds * 1000000.0);
}

- (int64_t)durationMicroseconds {
    NSTimeInterval seconds = 0;
    if (self.audioPlayer != nil) {
        seconds = self.audioPlayer.duration;
    } else if (self.midiPlayer != nil) {
        seconds = self.midiPlayer.duration;
    } else if (self.streamPlayer.currentItem != nil) {
        CMTime duration = self.streamPlayer.currentItem.duration;
        if (CMTIME_IS_NUMERIC(duration)) {
            seconds = CMTimeGetSeconds(duration);
        }
    }
    if (!isfinite(seconds) || seconds <= 0) return -1;
    return (int64_t)llround(seconds * 1000000.0);
}

- (int64_t)setMediaTimeMicroseconds:(int64_t)microseconds {
    NSTimeInterval seconds = MAX(0, (double)microseconds / 1000000.0);
    if (self.audioPlayer != nil) {
        BOOL resume = self.audioPlayer.isPlaying;
        NSTimeInterval duration = self.audioPlayer.duration;
        NSTimeInterval target = duration > 0 ? MIN(seconds, duration) : seconds;
        [self.audioPlayer pause];
        self.audioPlayer.currentTime = target;
        self.ended = NO;
        if (resume) {
            [self.audioPlayer play];
        }
        return [self mediaTimeMicroseconds];
    }
    if (self.midiPlayer != nil) {
        BOOL resume = self.midiPlayer.isPlaying;
        NSTimeInterval duration = self.midiPlayer.duration;
        NSTimeInterval target = duration > 0 ? MIN(seconds, duration) : seconds;
        [self.midiPlayer stop];
        self.midiPlayer.currentPosition = target;
        self.ended = NO;
        if (resume) {
            self.playbackGeneration += 1;
            [self playMIDIGeneration:self.playbackGeneration];
        }
        return [self mediaTimeMicroseconds];
    }
    if (self.streamPlayer != nil) {
        AVPlayerItem *item = self.streamPlayer.currentItem;
        if (item != nil && CMTIME_IS_NUMERIC(item.duration)) {
            NSTimeInterval duration = CMTimeGetSeconds(item.duration);
            if (isfinite(duration) && duration > 0) {
                seconds = MIN(seconds, duration);
            }
        }
        BOOL resume = self.streamPlayer.rate != 0.0f;
        if (@available(iOS 10.0, macOS 10.12, tvOS 10.0, *)) {
            resume = resume || self.streamPlayer.timeControlStatus ==
                    AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate;
        }
        self.pendingSeekMicroseconds =
                (int64_t)llround(seconds * 1000000.0);
        self.hasPendingSeek = YES;
        self.resumeAfterPendingSeek = resume;
        self.ended = NO;
        [self applyPendingSeekIfPossible];
        return self.pendingSeekMicroseconds;
    }
    return 0;
}

- (void)applyPendingSeekIfPossible {
    AVPlayerItem *item = self.streamPlayer.currentItem;
    if (!self.hasPendingSeek || item == nil ||
        item.status != AVPlayerItemStatusReadyToPlay) {
        return;
    }

    int64_t targetMicroseconds = self.pendingSeekMicroseconds;
    BOOL resume = self.resumeAfterPendingSeek;
    self.seekGeneration += 1;
    NSUInteger generation = self.seekGeneration;
    CMTime target = CMTimeMakeWithSeconds(
            MAX(0, (double)targetMicroseconds / 1000000.0), 1000000);
    [self.streamPlayer pause];

    __weak PMMediaEntry *weakSelf = self;
    [self.streamPlayer seekToTime:target
                 toleranceBefore:kCMTimeZero
                  toleranceAfter:kCMTimeZero
               completionHandler:^(BOOL finished) {
        dispatch_async(PMMediaQueue(), ^{
            PMMediaEntry *strongSelf = weakSelf;
            if (strongSelf == nil || generation != strongSelf.seekGeneration) {
                return;
            }
            strongSelf.hasPendingSeek = NO;
            strongSelf.ended = NO;
            if (finished && resume && !strongSelf.streamFailed) {
                [strongSelf.streamPlayer play];
            }
        });
    }];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)keyPath;
    (void)change;
    if (context != PMStreamStatusContext) {
        [super observeValueForKeyPath:keyPath
                             ofObject:object
                               change:change
                              context:context];
        return;
    }

    __weak PMMediaEntry *weakSelf = self;
    dispatch_async(PMMediaQueue(), ^{
        PMMediaEntry *strongSelf = weakSelf;
        AVPlayerItem *item = (AVPlayerItem *)object;
        if (strongSelf == nil || item != strongSelf.streamPlayer.currentItem) {
            return;
        }
        if (item.status == AVPlayerItemStatusFailed) {
            strongSelf.streamFailed = YES;
            strongSelf.hasPendingSeek = NO;
            NSLog(@"phoneME media: player item failed: %@",
                  item.error.localizedDescription ?: @"unknown error");
        } else if (item.status == AVPlayerItemStatusReadyToPlay) {
            [strongSelf applyPendingSeekIfPossible];
        }
    });
}

- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player
                       successfully:(BOOL)flag {
    dispatch_async(PMMediaQueue(), ^{
        if (self.transientTone) {
            [PMTonePlayers() removeObject:self];
        } else if (player == self.audioPlayer) {
            self.ended = YES;
        }
    });
}

@end

static PMMediaEntry *PMEntryForHandle(int32_t handle) {
    return PMMediaRegistry()[@(handle)];
}

static int32_t PMRegisterEntry(PMMediaEntry *entry) {
    if (entry == nil) return 0;
    int32_t handle = gNextMediaHandle++;
    if (handle <= 0) {
        gNextMediaHandle = 1;
        handle = gNextMediaHandle++;
    }
    while (PMMediaRegistry()[@(handle)] != nil) {
        handle = gNextMediaHandle++;
        if (handle <= 0) {
            gNextMediaHandle = 1;
            handle = gNextMediaHandle++;
        }
    }
    entry.handle = handle;
    PMMediaRegistry()[@(handle)] = entry;
    return handle;
}

static PMMediaEntry *PMEntryWithData(NSData *data, NSString *contentType) {
    if (data == nil || data.length == 0) return nil;
    PMConfigureAudioSession();

    PMMediaEntry *entry = [[PMMediaEntry alloc] init];
    NSError *error = nil;
    if (PMIsMIDIType(contentType)) {
        entry.midiPlayer = [[AVMIDIPlayer alloc] initWithData:data
                                                soundBankURL:nil
                                                      error:&error];
        if (entry.midiPlayer == nil) return nil;
    } else {
        entry.audioPlayer = [[AVAudioPlayer alloc] initWithData:data error:&error];
        if (entry.audioPlayer == nil) return nil;
        entry.audioPlayer.delegate = entry;
        [entry.audioPlayer prepareToPlay];
    }
    return entry;
}

static PMMediaEntry *PMEntryWithURL(NSURL *url, NSString *contentType) {
    if (url == nil) return nil;
    PMConfigureAudioSession();

    PMMediaEntry *entry = [[PMMediaEntry alloc] init];
    NSError *error = nil;
    if (url.isFileURL) {
        if (PMIsMIDIType(contentType) ||
            [@[@"mid", @"midi"] containsObject:url.pathExtension.lowercaseString]) {
            entry.midiPlayer = [[AVMIDIPlayer alloc] initWithContentsOfURL:url
                                                             soundBankURL:nil
                                                                   error:&error];
            if (entry.midiPlayer == nil) return nil;
        } else {
            entry.audioPlayer = [[AVAudioPlayer alloc] initWithContentsOfURL:url
                                                                       error:&error];
            if (entry.audioPlayer == nil) return nil;
            entry.audioPlayer.delegate = entry;
            [entry.audioPlayer prepareToPlay];
        }
        return entry;
    }

    NSString *scheme = url.scheme.lowercaseString;
    if (![scheme isEqualToString:@"http"] && ![scheme isEqualToString:@"https"]) {
        return nil;
    }

    NSDictionary *assetOptions = @{
        AVURLAssetAllowsCellularAccessKey: @YES
    };
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:assetOptions];
    AVPlayerItem *item = [AVPlayerItem playerItemWithAsset:asset];
    entry.streamPlayer = [AVPlayer playerWithPlayerItem:item];
    entry.streamPlayer.automaticallyWaitsToMinimizeStalling = YES;
    [item addObserver:entry
           forKeyPath:@"status"
              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
              context:PMStreamStatusContext];
    entry.observingStreamStatus = YES;
    __weak PMMediaEntry *weakEntry = entry;
    entry.streamFailedObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:AVPlayerItemFailedToPlayToEndTimeNotification
                    object:item
                     queue:nil
                usingBlock:^(NSNotification *notification) {
        dispatch_async(PMMediaQueue(), ^{
            PMMediaEntry *strongEntry = weakEntry;
            if (strongEntry == nil) return;
            strongEntry.streamFailed = YES;
            strongEntry.hasPendingSeek = NO;
            NSError *error = notification.userInfo[
                AVPlayerItemFailedToPlayToEndTimeErrorKey];
            NSLog(@"phoneME media: remote stream failed: %@",
                  error.localizedDescription ?: url.absoluteString);
        });
    }];
    entry.streamEndObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                    object:item
                     queue:nil
                usingBlock:^(NSNotification *notification) {
        (void)notification;
        dispatch_async(PMMediaQueue(), ^{
            PMMediaEntry *strongEntry = weakEntry;
            if (strongEntry == nil) return;
            if (strongEntry.loopCount == -1 || strongEntry.loopCount > 1) {
                if (strongEntry.loopCount > 1) {
                    strongEntry.loopCount -= 1;
                }
                [strongEntry.streamPlayer seekToTime:kCMTimeZero
                                  completionHandler:^(BOOL finished) {
                    if (finished) [strongEntry.streamPlayer play];
                }];
            } else {
                strongEntry.ended = YES;
            }
        });
    }];
    return entry;
}

static void PMWriteLE16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value & 0xff);
    destination[1] = (uint8_t)((value >> 8) & 0xff);
}

static void PMWriteLE32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value & 0xff);
    destination[1] = (uint8_t)((value >> 8) & 0xff);
    destination[2] = (uint8_t)((value >> 16) & 0xff);
    destination[3] = (uint8_t)((value >> 24) & 0xff);
}

static NSData *PMToneWAVData(int32_t note, int32_t durationMilliseconds,
                             int32_t volume) {
    const uint32_t sampleRate = 22050;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const double frequency = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
    const uint32_t sampleCount = MAX(1U,
        (uint32_t)(((uint64_t)sampleRate * (uint64_t)durationMilliseconds) / 1000ULL));
    const uint32_t dataSize = sampleCount * sizeof(int16_t);
    NSMutableData *data = [NSMutableData dataWithLength:44 + dataSize];
    uint8_t *bytes = data.mutableBytes;

    memcpy(bytes, "RIFF", 4);
    PMWriteLE32(bytes + 4, 36 + dataSize);
    memcpy(bytes + 8, "WAVEfmt ", 8);
    PMWriteLE32(bytes + 16, 16);
    PMWriteLE16(bytes + 20, 1);
    PMWriteLE16(bytes + 22, channels);
    PMWriteLE32(bytes + 24, sampleRate);
    PMWriteLE32(bytes + 28, sampleRate * channels * (bitsPerSample / 8));
    PMWriteLE16(bytes + 32, channels * (bitsPerSample / 8));
    PMWriteLE16(bytes + 34, bitsPerSample);
    memcpy(bytes + 36, "data", 4);
    PMWriteLE32(bytes + 40, dataSize);

    int16_t *samples = (int16_t *)(bytes + 44);
    double amplitude = 0.28 * ((double)MAX(0, MIN(100, volume)) / 100.0);
    const uint32_t fadeSamples = MIN(sampleCount / 2, sampleRate / 200);
    for (uint32_t index = 0; index < sampleCount; index++) {
        double envelope = 1.0;
        if (fadeSamples > 0 && index < fadeSamples) {
            envelope = (double)index / (double)fadeSamples;
        } else if (fadeSamples > 0 && index >= sampleCount - fadeSamples) {
            envelope = (double)(sampleCount - index - 1) / (double)fadeSamples;
        }
        double phase = (2.0 * M_PI * frequency * (double)index) / (double)sampleRate;
        samples[index] = (int16_t)lrint(sin(phase) * amplitude * envelope * INT16_MAX);
    }
    return data;
}

int32_t phoneme_ios_media_create_data(const uint8_t *data, int32_t length,
                                      const char *content_type) {
    if (data == NULL || length <= 0) return 0;
    __block int32_t handle = 0;
    NSData *mediaData = [NSData dataWithBytes:data length:(NSUInteger)length];
    NSString *type = PMStringFromUTF8(content_type);
    dispatch_sync(PMMediaQueue(), ^{
        handle = PMRegisterEntry(PMEntryWithData(mediaData, type));
    });
    return handle;
}

int32_t phoneme_ios_media_create_locator(const char *locator,
                                         const char *content_type) {
    NSString *locatorString = PMStringFromUTF8(locator);
    if (locatorString.length == 0) return 0;
    NSString *type = PMStringFromUTF8(content_type);
    __block int32_t handle = 0;
    dispatch_sync(PMMediaQueue(), ^{
        handle = PMRegisterEntry(PMEntryWithURL(PMURLFromLocator(locatorString), type));
    });
    return handle;
}

int32_t phoneme_ios_media_start(int32_t handle) {
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) start];
    });
    return result ? 1 : 0;
}

int32_t phoneme_ios_media_stop(int32_t handle) {
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) stop];
    });
    return result ? 1 : 0;
}

void phoneme_ios_media_close(int32_t handle) {
    dispatch_sync(PMMediaQueue(), ^{
        PMMediaEntry *entry = PMEntryForHandle(handle);
        [entry stop];
        [PMMediaRegistry() removeObjectForKey:@(handle)];
    });
}

void phoneme_ios_media_set_loop_count(int32_t handle, int32_t count) {
    dispatch_sync(PMMediaQueue(), ^{
        [PMEntryForHandle(handle) configureLoopCount:count];
    });
}

void phoneme_ios_media_set_volume(int32_t handle, int32_t level) {
    dispatch_sync(PMMediaQueue(), ^{
        PMMediaEntry *entry = PMEntryForHandle(handle);
        entry.volume = MAX(0.0f, MIN(1.0f, (float)level / 100.0f));
        [entry applyVolume];
    });
}

void phoneme_ios_media_set_mute(int32_t handle, int32_t muted) {
    dispatch_sync(PMMediaQueue(), ^{
        PMMediaEntry *entry = PMEntryForHandle(handle);
        entry.muted = muted != 0;
        [entry applyVolume];
    });
}

int64_t phoneme_ios_media_set_time(int32_t handle, int64_t microseconds) {
    __block int64_t result = 0;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) setMediaTimeMicroseconds:microseconds];
    });
    return result;
}

int64_t phoneme_ios_media_get_time(int32_t handle) {
    __block int64_t result = 0;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) mediaTimeMicroseconds];
    });
    return result;
}

int64_t phoneme_ios_media_get_duration(int32_t handle) {
    __block int64_t result = -1;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) durationMicroseconds];
    });
    return result;
}

int32_t phoneme_ios_media_is_playing(int32_t handle) {
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) isPlaying];
    });
    return result ? 1 : 0;
}

int32_t phoneme_ios_media_has_ended(int32_t handle) {
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        result = PMEntryForHandle(handle).ended;
    });
    return result ? 1 : 0;
}

int32_t phoneme_ios_media_has_error(int32_t handle) {
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) hasError];
    });
    return result ? 1 : 0;
}

int32_t phoneme_ios_media_play_tone(int32_t note, int32_t duration_ms,
                                    int32_t volume) {
    if (note < 0 || note > 127 || duration_ms <= 0) return 0;
    PMConfigureAudioSession();
    NSData *toneData = PMToneWAVData(note, duration_ms, volume);
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        PMMediaEntry *entry = PMEntryWithData(toneData, @"audio/x-wav");
        if (entry == nil) return;
        entry.transientTone = YES;
        [PMTonePlayers() addObject:entry];
        result = [entry start];
        if (!result) [PMTonePlayers() removeObject:entry];
    });
    return result ? 1 : 0;
}

#if TARGET_OS_IOS || TARGET_OS_TV
static uint64_t gPMVibrationGeneration = 0;
static uint64_t gPMLightGeneration = 0;
static BOOL gPMKeepScreenAwake = NO;
#endif

void phoneme_ios_media_reset(void) {
    dispatch_sync(PMMediaQueue(), ^{
        for (PMMediaEntry *entry in PMMediaRegistry().allValues) {
            [entry stop];
        }
        [PMMediaRegistry() removeAllObjects];
        for (PMMediaEntry *entry in PMTonePlayers()) {
            [entry stop];
        }
        [PMTonePlayers() removeAllObjects];
    });

#if TARGET_OS_IOS || TARGET_OS_TV
    NSError *deactivationError = nil;
    [AVAudioSession.sharedInstance
        setActive:NO
        withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
        error:&deactivationError];
    if (deactivationError != nil) {
        NSLog(@"phoneME media: unable to deactivate audio session: %@",
              deactivationError.localizedDescription);
    }

    void (^resetDeviceState)(void) = ^{
        ++gPMVibrationGeneration;
        ++gPMLightGeneration;
        gPMKeepScreenAwake = NO;
        UIApplication.sharedApplication.idleTimerDisabled = NO;
    };
    if (NSThread.isMainThread) {
        resetDeviceState();
    } else {
        dispatch_sync(dispatch_get_main_queue(), resetDeviceState);
    }
#endif
}

int phoneme_ios_device_start_vibrate(int frequency, int64_t duration_ms) {
#if TARGET_OS_IOS
    if (frequency <= 0 || duration_ms <= 0) return 1;
    dispatch_async(dispatch_get_main_queue(), ^{
        uint64_t generation = ++gPMVibrationGeneration;
        NSTimeInterval interval = 0.85 -
                (0.70 * (double)MIN(100, frequency) / 100.0);
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:
                (double)duration_ms / 1000.0];
        __block void (^pulse)(void);
        pulse = ^{
            if (generation != gPMVibrationGeneration ||
                    [deadline timeIntervalSinceNow] <= 0) {
                pulse = nil;
                return;
            }
            AudioServicesPlaySystemSound(kSystemSoundID_Vibrate);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                    (int64_t)(interval * NSEC_PER_SEC)),
                    dispatch_get_main_queue(), pulse);
        };
        pulse();
    });
    return 1;
#else
    (void)frequency;
    (void)duration_ms;
    return 0;
#endif
}

int phoneme_ios_device_stop_vibrate(void) {
#if TARGET_OS_IOS
    dispatch_async(dispatch_get_main_queue(), ^{
        ++gPMVibrationGeneration;
    });
    return 1;
#else
    return 0;
#endif
}

int phoneme_ios_device_set_vibrate(int enabled) {
    return enabled
        ? phoneme_ios_device_start_vibrate(100, 60000)
        : phoneme_ios_device_stop_vibrate();
}

int phoneme_ios_device_set_backlight(int mode) {
#if TARGET_OS_IOS || TARGET_OS_TV
    dispatch_async(dispatch_get_main_queue(), ^{
        switch (mode) {
            case 0:
                gPMKeepScreenAwake = NO;
                break;
            case 1:
                gPMKeepScreenAwake = YES;
                break;
            case 2:
                gPMKeepScreenAwake = !gPMKeepScreenAwake;
                break;
            case 3:
                break;
            default:
                return;
        }
        UIApplication.sharedApplication.idleTimerDisabled =
                gPMKeepScreenAwake;
    });
    return mode >= 0 && mode <= 3 ? 1 : 0;
#else
    (void)mode;
    return 0;
#endif
}

int phoneme_ios_device_flash_lights(int64_t duration_ms) {
#if TARGET_OS_IOS || TARGET_OS_TV
    if (duration_ms <= 0) return 1;
    dispatch_async(dispatch_get_main_queue(), ^{
        uint64_t generation = ++gPMLightGeneration;
        BOOL previous = UIApplication.sharedApplication.idleTimerDisabled;
        UIApplication.sharedApplication.idleTimerDisabled = YES;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                duration_ms * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            if (generation == gPMLightGeneration) {
                UIApplication.sharedApplication.idleTimerDisabled = previous;
            }
        });
    });
    return 1;
#else
    (void)duration_ms;
    return 0;
#endif
}
