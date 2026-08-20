#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS || TARGET_OS_TV
#import <UIKit/UIKit.h>
#endif
#if TARGET_OS_IOS
#import <MediaPlayer/MediaPlayer.h>
#endif

#include <math.h>
#include <stdint.h>

@class PMMediaEntry;

static void PMReevaluateNowPlaying(PMMediaEntry *preferredEntry);
static void PMClearNowPlaying(void);

static uint8_t gPMMediaQueueSpecificKey;

static dispatch_queue_t PMMediaQueue(void) {
    static dispatch_queue_t queue;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        queue = dispatch_queue_create("dev.phoneme.media", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_set_specific(queue,
                                    &gPMMediaQueueSpecificKey,
                                    &gPMMediaQueueSpecificKey,
                                    NULL);
    });
    return queue;
}

static void PMPerformMediaQueueSync(dispatch_block_t block) {
    if (dispatch_get_specific(&gPMMediaQueueSpecificKey) != NULL) {
        block();
    } else {
        dispatch_sync(PMMediaQueue(), block);
    }
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
static uint64_t gPMPlaybackSequence = 0;
static void *PMStreamStatusContext = &PMStreamStatusContext;
static void *PMStreamDurationContext = &PMStreamDurationContext;

#if TARGET_OS_IOS
static NSString *gPMMediaApplicationTitle;
static NSString *gPMMediaApplicationArtist;
static UIImage *gPMMediaApplicationArtwork;
static int32_t gPMNowPlayingHandle = 0;
#endif

#if TARGET_OS_IOS || TARGET_OS_TV
static void PMRegisterMediaLifecycleObservers(void);
void phoneme_ios_media_reset(void);
#endif

static void PMConfigureAudioSession(void) {
#if TARGET_OS_IOS || TARGET_OS_TV
    PMRegisterMediaLifecycleObservers();
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
#endif
}

static BOOL PMActivateAudioSession(void) {
#if TARGET_OS_IOS || TARGET_OS_TV
    PMConfigureAudioSession();
    NSError *activationError = nil;
    [AVAudioSession.sharedInstance setActive:YES error:&activationError];
    if (activationError != nil) {
        NSLog(@"phoneME media: unable to activate audio session: %@",
              activationError.localizedDescription);
        return NO;
    }
#endif
    return YES;
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

static NSString *PMMediaTitleFromURL(NSURL *url) {
    NSString *name = url.lastPathComponent.stringByDeletingPathExtension;
    name = name.stringByRemovingPercentEncoding ?: name;
    name = [name stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (name.length > 0) {
        return name;
    }
    NSString *host = [url.host stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    return host.length > 0 ? host : nil;
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
@property(nonatomic) BOOL resumeAfterSystemSuspend;
@property(nonatomic) BOOL seekInProgress;
@property(nonatomic) NSUInteger seekGeneration;
@property(nonatomic) NSUInteger pendingSeekRetryCount;
@property(nonatomic) int32_t handle;
@property(nonatomic) NSInteger loopCount;
@property(nonatomic) NSInteger midiLoopsRemaining;
@property(nonatomic) BOOL ended;
@property(nonatomic) BOOL muted;
@property(nonatomic) float volume;
@property(nonatomic) NSUInteger playbackGeneration;
@property(nonatomic) BOOL transientTone;
@property(nonatomic) BOOL hasStartedPlayback;
@property(nonatomic) uint64_t startedSequence;
@property(nonatomic, copy) NSString *mediaTitle;
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
        [_streamPlayer.currentItem removeObserver:self
                                       forKeyPath:@"duration"
                                          context:PMStreamDurationContext];
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
    if (!PMActivateAudioSession()) {
        return NO;
    }
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
        BOOL started = [self.audioPlayer play];
        if (started) {
            self.hasStartedPlayback = YES;
            self.startedSequence = ++gPMPlaybackSequence;
        }
        return started;
    }

    if (self.midiPlayer != nil) {
        if (self.midiPlayer.currentPosition >= self.midiPlayer.duration &&
            self.midiPlayer.duration > 0) {
            self.midiPlayer.currentPosition = 0;
        }
        self.midiLoopsRemaining = self.loopCount;
        [self applyVolume];
        self.hasStartedPlayback = YES;
        self.startedSequence = ++gPMPlaybackSequence;
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
            self.hasStartedPlayback = YES;
            self.startedSequence = ++gPMPlaybackSequence;
            [self applyPendingSeekIfPossible];
            return YES;
        }
        [self.streamPlayer play];
        if (item.status == AVPlayerItemStatusFailed || self.streamPlayer.error != nil) {
            self.streamFailed = YES;
            return NO;
        }
        self.hasStartedPlayback = YES;
        self.startedSequence = ++gPMPlaybackSequence;
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
                PMReevaluateNowPlaying(nil);
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
        BOOL resume = self.resumeAfterPendingSeek ||
                      self.streamPlayer.rate != 0.0f;
        if (@available(iOS 10.0, macOS 10.12, tvOS 10.0, *)) {
            resume = resume || self.streamPlayer.timeControlStatus ==
                    AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate;
        }
        self.pendingSeekMicroseconds =
                (int64_t)llround(seconds * 1000000.0);
        self.hasPendingSeek = YES;
        self.resumeAfterPendingSeek = resume;
        self.ended = NO;
        self.pendingSeekRetryCount = 0;
        self.seekGeneration += 1;
        [item cancelPendingSeeks];
        self.seekInProgress = NO;
        [self applyPendingSeekIfPossible];
        return self.pendingSeekMicroseconds;
    }
    return 0;
}

- (void)applyPendingSeekIfPossible {
    AVPlayerItem *item = self.streamPlayer.currentItem;
    if (!self.hasPendingSeek || self.seekInProgress || item == nil ||
        item.status != AVPlayerItemStatusReadyToPlay) {
        return;
    }

    NSTimeInterval seconds = MAX(
        0, (double)self.pendingSeekMicroseconds / 1000000.0);
    if (CMTIME_IS_NUMERIC(item.duration)) {
        NSTimeInterval duration = CMTimeGetSeconds(item.duration);
        if (isfinite(duration) && duration > 0) {
            seconds = MIN(seconds, duration);
        }
    }

    CMTime target = CMTimeMakeWithSeconds(seconds, 1000000);
    CMTime tolerance = CMTimeMakeWithSeconds(0.1, 1000);

    NSTimeInterval resolvedSeconds = CMTimeGetSeconds(target);
    if (isfinite(resolvedSeconds) && resolvedSeconds >= 0) {
        seconds = resolvedSeconds;
    }
    self.pendingSeekMicroseconds =
        (int64_t)llround(seconds * 1000000.0);
    NSUInteger generation = self.seekGeneration;
    self.seekInProgress = YES;
    [self.streamPlayer pause];

    __weak PMMediaEntry *weakSelf = self;
    [self.streamPlayer seekToTime:target
                 toleranceBefore:tolerance
                  toleranceAfter:tolerance
               completionHandler:^(BOOL finished) {
        dispatch_async(PMMediaQueue(), ^{
            PMMediaEntry *strongSelf = weakSelf;
            if (strongSelf == nil || generation != strongSelf.seekGeneration) {
                return;
            }
            strongSelf.seekInProgress = NO;
            strongSelf.ended = NO;
            if (finished) {
                BOOL shouldResume = strongSelf.resumeAfterPendingSeek;
                strongSelf.hasPendingSeek = NO;
                strongSelf.resumeAfterPendingSeek = NO;
                strongSelf.pendingSeekRetryCount = 0;
                if (shouldResume && !strongSelf.streamFailed) {
                    [strongSelf.streamPlayer play];
                }
                PMReevaluateNowPlaying(nil);
                return;
            }

            strongSelf.pendingSeekRetryCount += 1;
            if (strongSelf.pendingSeekRetryCount > 5) {
                BOOL shouldResume = strongSelf.resumeAfterPendingSeek;
                strongSelf.hasPendingSeek = NO;
                strongSelf.resumeAfterPendingSeek = NO;
                strongSelf.pendingSeekRetryCount = 0;
                if (shouldResume && !strongSelf.streamFailed) {
                    [strongSelf.streamPlayer play];
                }
                PMReevaluateNowPlaying(nil);
                return;
            }
            NSUInteger retryGeneration = strongSelf.seekGeneration;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                         (int64_t)(0.15 * NSEC_PER_SEC)),
                           PMMediaQueue(), ^{
                PMMediaEntry *retrySelf = weakSelf;
                if (retrySelf == nil ||
                    retryGeneration != retrySelf.seekGeneration ||
                    !retrySelf.hasPendingSeek) {
                    return;
                }
                [retrySelf applyPendingSeekIfPossible];
            });
        });
    }];

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 (int64_t)(8.0 * NSEC_PER_SEC)),
                   PMMediaQueue(), ^{
        PMMediaEntry *strongSelf = weakSelf;
        if (strongSelf == nil || generation != strongSelf.seekGeneration ||
            !strongSelf.seekInProgress || !strongSelf.hasPendingSeek) {
            return;
        }
        BOOL shouldResume = strongSelf.resumeAfterPendingSeek;
        strongSelf.seekGeneration += 1;
        [strongSelf.streamPlayer.currentItem cancelPendingSeeks];
        strongSelf.seekInProgress = NO;
        strongSelf.hasPendingSeek = NO;
        strongSelf.resumeAfterPendingSeek = NO;
        strongSelf.pendingSeekRetryCount = 0;
        if (shouldResume && !strongSelf.streamFailed) {
            [strongSelf.streamPlayer play];
        }
        PMReevaluateNowPlaying(nil);
    });
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)keyPath;
    (void)change;
    BOOL isStreamObservation = context == PMStreamStatusContext ||
        context == PMStreamDurationContext;
    if (!isStreamObservation) {
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
            strongSelf.seekInProgress = NO;
            NSLog(@"phoneME media: player item failed: %@",
                  item.error.localizedDescription ?: @"unknown error");
            PMReevaluateNowPlaying(nil);
        } else if (item.status == AVPlayerItemStatusReadyToPlay) {
            [strongSelf applyPendingSeekIfPossible];
            PMReevaluateNowPlaying(nil);
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
            PMReevaluateNowPlaying(nil);
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

#if TARGET_OS_IOS
static MPRemoteCommandHandlerStatus PMRemotePlay(void) {
    __block MPRemoteCommandHandlerStatus status =
        MPRemoteCommandHandlerStatusNoSuchContent;
    PMPerformMediaQueueSync(^{
        PMMediaEntry *entry = PMEntryForHandle(gPMNowPlayingHandle);
        if (entry == nil) return;
        if ([entry start]) {
            PMReevaluateNowPlaying(entry);
            status = MPRemoteCommandHandlerStatusSuccess;
        } else {
            status = MPRemoteCommandHandlerStatusCommandFailed;
        }
    });
    return status;
}

static MPRemoteCommandHandlerStatus PMRemotePause(void) {
    __block MPRemoteCommandHandlerStatus status =
        MPRemoteCommandHandlerStatusNoSuchContent;
    PMPerformMediaQueueSync(^{
        PMMediaEntry *entry = PMEntryForHandle(gPMNowPlayingHandle);
        if (entry == nil) return;
        if ([entry stop]) {
            PMReevaluateNowPlaying(nil);
            status = MPRemoteCommandHandlerStatusSuccess;
        } else {
            status = MPRemoteCommandHandlerStatusCommandFailed;
        }
    });
    return status;
}

static MPRemoteCommandHandlerStatus PMRemoteToggle(void) {
    __block BOOL playing = NO;
    PMPerformMediaQueueSync(^{
        playing = [PMEntryForHandle(gPMNowPlayingHandle) isPlaying];
    });
    return playing ? PMRemotePause() : PMRemotePlay();
}

static MPRemoteCommandHandlerStatus PMRemoteSetPosition(NSTimeInterval seconds) {
    if (!isfinite(seconds) || seconds < 0) {
        return MPRemoteCommandHandlerStatusCommandFailed;
    }
    __block MPRemoteCommandHandlerStatus status =
        MPRemoteCommandHandlerStatusNoSuchContent;
    PMPerformMediaQueueSync(^{
        PMMediaEntry *entry = PMEntryForHandle(gPMNowPlayingHandle);
        if (entry == nil) return;
        [entry setMediaTimeMicroseconds:
            (int64_t)llround(seconds * 1000000.0)];
        PMReevaluateNowPlaying(nil);
        status = MPRemoteCommandHandlerStatusSuccess;
    });
    return status;
}

static MPRemoteCommandHandlerStatus PMRemoteSkip(NSTimeInterval interval) {
    __block NSTimeInterval target = -1;
    PMPerformMediaQueueSync(^{
        PMMediaEntry *entry = PMEntryForHandle(gPMNowPlayingHandle);
        if (entry == nil) return;
        target = MAX(0, (double)[entry mediaTimeMicroseconds] / 1000000.0 +
                        interval);
    });
    return target >= 0
        ? PMRemoteSetPosition(target)
        : MPRemoteCommandHandlerStatusNoSuchContent;
}

static void PMEnsureRemoteCommandsInstalled(void) {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        dispatch_async(dispatch_get_main_queue(), ^{
            MPRemoteCommandCenter *center =
                MPRemoteCommandCenter.sharedCommandCenter;
            [center.playCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    (void)event;
                    return PMRemotePlay();
                }];
            [center.pauseCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    (void)event;
                    return PMRemotePause();
                }];
            [center.togglePlayPauseCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    (void)event;
                    return PMRemoteToggle();
                }];
            [center.stopCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    (void)event;
                    return PMRemotePause();
                }];
            [center.changePlaybackPositionCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    if (![event isKindOfClass:
                            MPChangePlaybackPositionCommandEvent.class]) {
                        return MPRemoteCommandHandlerStatusCommandFailed;
                    }
                    MPChangePlaybackPositionCommandEvent *positionEvent =
                        (MPChangePlaybackPositionCommandEvent *)event;
                    return PMRemoteSetPosition(positionEvent.positionTime);
                }];
            [center.skipForwardCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    NSTimeInterval interval = 10;
                    if ([event isKindOfClass:MPSkipIntervalCommandEvent.class]) {
                        interval = ((MPSkipIntervalCommandEvent *)event).interval;
                    }
                    return PMRemoteSkip(interval);
                }];
            [center.skipBackwardCommand addTargetWithHandler:
                ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
                    NSTimeInterval interval = 10;
                    if ([event isKindOfClass:MPSkipIntervalCommandEvent.class]) {
                        interval = ((MPSkipIntervalCommandEvent *)event).interval;
                    }
                    return PMRemoteSkip(-interval);
                }];
            center.skipForwardCommand.preferredIntervals = @[@10];
            center.skipBackwardCommand.preferredIntervals = @[@10];
            center.nextTrackCommand.enabled = NO;
            center.previousTrackCommand.enabled = NO;
            center.seekForwardCommand.enabled = NO;
            center.seekBackwardCommand.enabled = NO;
        });
    });
}

static BOOL PMEntryIsNowPlayingEligible(PMMediaEntry *entry) {
    if (entry == nil || entry.transientTone || !entry.hasStartedPlayback ||
        entry.ended || entry.streamFailed) {
        return NO;
    }
    int64_t duration = [entry durationMicroseconds];
    return entry.streamPlayer != nil || entry.midiPlayer != nil ||
           entry.loopCount != 1 || duration >= 2000000;
}

static NSInteger PMNowPlayingScore(PMMediaEntry *entry,
                                   PMMediaEntry *preferredEntry,
                                   PMMediaEntry *currentEntry) {
    NSInteger score = [entry isPlaying] ? 300 : 0;
    if (entry.streamPlayer != nil) score += 400;
    if (entry.midiPlayer != nil) score += 180;
    if (entry.loopCount == -1) {
        score += 500;
    } else if (entry.loopCount > 1) {
        score += 180;
    }

    int64_t duration = [entry durationMicroseconds];
    if (duration < 0) {
        score += 150;
    } else if (duration >= 600000000) {
        score += 600;
    } else if (duration >= 60000000) {
        score += 450;
    } else if (duration >= 10000000) {
        score += 300;
    } else if (duration >= 2000000) {
        score += 100;
    }

    if (entry == currentEntry) score += 80;
    if (entry == preferredEntry) score += 120;
    return score;
}

static void PMPublishNowPlaying(PMMediaEntry *entry) {
    PMEnsureRemoteCommandsInstalled();

    NSString *applicationTitle = gPMMediaApplicationTitle.length > 0
        ? gPMMediaApplicationTitle
        : (NSBundle.mainBundle.infoDictionary[@"CFBundleDisplayName"]
            ?: NSBundle.mainBundle.infoDictionary[@"CFBundleName"]
            ?: @"phoneME");
    NSString *applicationArtist = gPMMediaApplicationArtist.length > 0
        ? gPMMediaApplicationArtist
        : @"phoneME";
    NSString *mediaTitle = entry.mediaTitle.length > 0
        ? entry.mediaTitle
        : nil;
    NSString *title = mediaTitle ?: applicationTitle;
    NSString *artist = mediaTitle != nil ? applicationTitle : applicationArtist;
    UIImage *artworkImage = gPMMediaApplicationArtwork;
    int64_t durationMicroseconds = [entry durationMicroseconds];
    NSTimeInterval duration = durationMicroseconds > 0
        ? (double)durationMicroseconds / 1000000.0
        : 0;
    NSTimeInterval elapsed =
        MAX(0, (double)[entry mediaTimeMicroseconds] / 1000000.0);
    BOOL playing = [entry isPlaying];
    BOOL liveStream = entry.streamPlayer != nil && duration <= 0;
    int32_t handle = entry.handle;

    dispatch_async(dispatch_get_main_queue(), ^{
        NSMutableDictionary<NSString *, id> *info =
            [[NSMutableDictionary alloc] init];
        info[MPMediaItemPropertyTitle] = title;
        info[MPMediaItemPropertyArtist] = artist;
        info[MPMediaItemPropertyAlbumTitle] = @"phoneME";
        info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(elapsed);
        info[MPNowPlayingInfoPropertyPlaybackRate] = playing ? @1.0 : @0.0;
        info[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @1.0;
        info[MPNowPlayingInfoPropertyMediaType] =
            @(MPNowPlayingInfoMediaTypeAudio);
        info[MPNowPlayingInfoPropertyIsLiveStream] = @(liveStream);
        info[MPNowPlayingInfoPropertyExternalContentIdentifier] =
            [NSString stringWithFormat:@"phoneme-media-%d", handle];
        if (duration > 0) {
            info[MPMediaItemPropertyPlaybackDuration] = @(duration);
        }
        if (artworkImage != nil) {
            info[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc]
                initWithBoundsSize:artworkImage.size
                requestHandler:^UIImage * _Nonnull(CGSize size) {
                    (void)size;
                    return artworkImage;
                }];
        }

        MPNowPlayingInfoCenter *center = MPNowPlayingInfoCenter.defaultCenter;
        center.nowPlayingInfo = info;
        center.playbackState = playing
            ? MPNowPlayingPlaybackStatePlaying
            : MPNowPlayingPlaybackStatePaused;

        MPRemoteCommandCenter *commands =
            MPRemoteCommandCenter.sharedCommandCenter;
        commands.playCommand.enabled = !playing;
        commands.pauseCommand.enabled = playing;
        commands.togglePlayPauseCommand.enabled = YES;
        commands.stopCommand.enabled = playing;
        BOOL seekable = duration > 0;
        commands.changePlaybackPositionCommand.enabled = seekable;
        commands.skipForwardCommand.enabled = seekable;
        commands.skipBackwardCommand.enabled = seekable;
    });
}
#endif

static void PMReevaluateNowPlaying(PMMediaEntry *preferredEntry) {
#if TARGET_OS_IOS
    PMMediaEntry *currentEntry = PMEntryForHandle(gPMNowPlayingHandle);
    PMMediaEntry *bestEntry = nil;
    NSInteger bestScore = NSIntegerMin;
    uint64_t bestSequence = 0;

    for (PMMediaEntry *entry in PMMediaRegistry().allValues) {
        if (!PMEntryIsNowPlayingEligible(entry)) continue;
        if (![entry isPlaying] && entry != currentEntry) continue;
        NSInteger score = PMNowPlayingScore(entry,
                                            preferredEntry,
                                            currentEntry);
        if (bestEntry == nil || score > bestScore ||
            (score == bestScore && entry.startedSequence > bestSequence)) {
            bestEntry = entry;
            bestScore = score;
            bestSequence = entry.startedSequence;
        }
    }

    if (bestEntry == nil) {
        PMClearNowPlaying();
        return;
    }
    gPMNowPlayingHandle = bestEntry.handle;
    PMPublishNowPlaying(bestEntry);
#else
    (void)preferredEntry;
#endif
}

static void PMClearNowPlaying(void) {
#if TARGET_OS_IOS
    gPMNowPlayingHandle = 0;
    PMEnsureRemoteCommandsInstalled();
    dispatch_async(dispatch_get_main_queue(), ^{
        MPNowPlayingInfoCenter *center = MPNowPlayingInfoCenter.defaultCenter;
        center.nowPlayingInfo = nil;
        center.playbackState = MPNowPlayingPlaybackStateStopped;

        MPRemoteCommandCenter *commands =
            MPRemoteCommandCenter.sharedCommandCenter;
        commands.playCommand.enabled = NO;
        commands.pauseCommand.enabled = NO;
        commands.togglePlayPauseCommand.enabled = NO;
        commands.stopCommand.enabled = NO;
        commands.changePlaybackPositionCommand.enabled = NO;
        commands.skipForwardCommand.enabled = NO;
        commands.skipBackwardCommand.enabled = NO;
    });
#endif
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
    entry.mediaTitle = PMMediaTitleFromURL(url);
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
    item.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
    entry.streamPlayer = [AVPlayer playerWithPlayerItem:item];
    entry.streamPlayer.automaticallyWaitsToMinimizeStalling = YES;
    [item addObserver:entry
           forKeyPath:@"status"
              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
              context:PMStreamStatusContext];
    [item addObserver:entry
           forKeyPath:@"duration"
              options:NSKeyValueObservingOptionInitial | NSKeyValueObservingOptionNew
              context:PMStreamDurationContext];
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
            PMReevaluateNowPlaying(nil);
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
                PMReevaluateNowPlaying(nil);
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

void phoneme_ios_media_set_application_metadata(const char *title,
                                                const char *artist,
                                                const char *artwork_path) {
#if TARGET_OS_IOS
    NSString *applicationTitle = [PMStringFromUTF8(title) copy];
    NSString *applicationArtist = [PMStringFromUTF8(artist) copy];
    NSString *artworkPath = PMStringFromUTF8(artwork_path);
    UIImage *artwork = artworkPath.length > 0
        ? [UIImage imageWithContentsOfFile:artworkPath]
        : nil;
    PMPerformMediaQueueSync(^{
        gPMMediaApplicationTitle = applicationTitle;
        gPMMediaApplicationArtist = applicationArtist;
        gPMMediaApplicationArtwork = artwork;
        PMReevaluateNowPlaying(nil);
    });
#else
    (void)title;
    (void)artist;
    (void)artwork_path;
#endif
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
        PMMediaEntry *entry = PMEntryForHandle(handle);
        result = [entry start];
        if (result) {
            PMReevaluateNowPlaying(entry);
        }
    });
    return result ? 1 : 0;
}

int32_t phoneme_ios_media_stop(int32_t handle) {
    __block BOOL result = NO;
    dispatch_sync(PMMediaQueue(), ^{
        result = [PMEntryForHandle(handle) stop];
        PMReevaluateNowPlaying(nil);
    });
    return result ? 1 : 0;
}

void phoneme_ios_media_close(int32_t handle) {
    dispatch_sync(PMMediaQueue(), ^{
        PMMediaEntry *entry = PMEntryForHandle(handle);
        [entry stop];
        [PMMediaRegistry() removeObjectForKey:@(handle)];
        PMReevaluateNowPlaying(nil);
    });
}

void phoneme_ios_media_set_loop_count(int32_t handle, int32_t count) {
    dispatch_sync(PMMediaQueue(), ^{
        [PMEntryForHandle(handle) configureLoopCount:count];
        PMReevaluateNowPlaying(nil);
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
        PMReevaluateNowPlaying(nil);
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

int32_t phoneme_ios_media_has_active_playback(void) {
    __block BOOL result = NO;
    PMPerformMediaQueueSync(^{
        for (PMMediaEntry *entry in PMMediaRegistry().allValues) {
            if ([entry isPlaying]) {
                result = YES;
                return;
            }
            if (entry.streamPlayer != nil) {
                BOOL waitingForData = entry.resumeAfterPendingSeek;
                if (@available(iOS 10.0, macOS 10.12, tvOS 10.0, *)) {
                    waitingForData = waitingForData ||
                        entry.streamPlayer.timeControlStatus ==
                            AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate;
                }
                if (waitingForData && !entry.streamFailed && !entry.ended) {
                    result = YES;
                    return;
                }
            }
        }
        for (PMMediaEntry *entry in PMTonePlayers()) {
            if ([entry isPlaying]) {
                result = YES;
                return;
            }
        }
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

static void PMStopAllMediaForSuspension(void) {
    dispatch_sync(PMMediaQueue(), ^{
        for (PMMediaEntry *entry in PMMediaRegistry().allValues) {
            entry.resumeAfterSystemSuspend = [entry isPlaying] ||
                entry.resumeAfterPendingSeek;
            if (entry.resumeAfterSystemSuspend) {
                [entry stop];
            }
        }
        for (PMMediaEntry *entry in PMTonePlayers()) {
            [entry stop];
        }
        [PMTonePlayers() removeAllObjects];
        PMReevaluateNowPlaying(nil);
    });
}

void phoneme_ios_media_suspend(void) {
    PMStopAllMediaForSuspension();

#if TARGET_OS_IOS || TARGET_OS_TV
    NSError *deactivationError = nil;
    [AVAudioSession.sharedInstance
        setActive:NO
        withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
        error:&deactivationError];
    if (deactivationError != nil) {
        NSLog(@"phoneME media: unable to suspend audio session: %@",
              deactivationError.localizedDescription);
    }
#endif
}

void phoneme_ios_media_resume(void) {
    dispatch_sync(PMMediaQueue(), ^{
        for (PMMediaEntry *entry in PMMediaRegistry().allValues) {
            if (!entry.resumeAfterSystemSuspend) {
                continue;
            }
            entry.resumeAfterSystemSuspend = NO;
            (void)[entry start];
        }
        PMReevaluateNowPlaying(nil);
    });
}

#if TARGET_OS_IOS || TARGET_OS_TV
// Reuses the app-suspend machinery for system audio interruptions (phone
// call, Siri, alarm): stop players, then restart them when the OS ends the
// interruption with the resume hint. Without this, the session stayed
// deactivated after an interruption and in-game audio died until the next
// playback start.
static void PMRegisterMediaLifecycleObservers(void) {
    static dispatch_once_t onceToken;
    static id interruptionObserver = nil;
    static id mediaResetObserver = nil;
    dispatch_once(&onceToken, ^{
        NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
        interruptionObserver = [center
            addObserverForName:AVAudioSessionInterruptionNotification
                         object:AVAudioSession.sharedInstance
                          queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *note) {
                NSUInteger type = [note.userInfo[AVAudioSessionInterruptionTypeKey]
                    unsignedIntegerValue];
                if (type == AVAudioSessionInterruptionTypeBegan) {
                    // The system already deactivated the session; skip
                    // setActive:NO — NotifyOthersOnDeactivation would wrongly
                    // unpause other apps' audio mid-interruption.
                    PMStopAllMediaForSuspension();
                } else if (type == AVAudioSessionInterruptionTypeEnded) {
                    NSUInteger options =
                        [note.userInfo[AVAudioSessionInterruptionOptionKey]
                            unsignedIntegerValue];
                    if ((options & AVAudioSessionInterruptionOptionShouldResume)
                            != 0) {
                        phoneme_ios_media_resume();
                    }
                }
            }];
        // Media server death invalidates every player object. Reset the
        // registry so MIDlets recreate players on their next play.
        // ponytail: existing handles go stale until the game rebuilds its
        // Player objects; full revival would need source retention on
        // PMMediaEntry.
        mediaResetObserver = [center
            addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                         object:AVAudioSession.sharedInstance
                          queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *_) {
                phoneme_ios_media_reset();
            }];
    });
}
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
#if TARGET_OS_IOS
        gPMMediaApplicationTitle = nil;
        gPMMediaApplicationArtist = nil;
        gPMMediaApplicationArtwork = nil;
#endif
        PMClearNowPlaying();
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
