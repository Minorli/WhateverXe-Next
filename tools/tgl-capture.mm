// SPDX-License-Identifier: MIT
// Read-only, bounded live capture. Raw data may contain GPU/kernel addresses and
// client memory; keep output in ignored local/ storage, never publish it.
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include "../src/RecoveryState.hpp"
static volatile sig_atomic_t stopped;
static void stop(int) { stopped=1; }
// Reports are an independent signal: not every driver build publishes the
// recovery keys above. Keep filenames private with the rest of each snapshot.
static NSSet<NSString *> *gpuReports(bool &readable,NSArray<NSString *> *directories) {
    NSMutableSet<NSString *> *reports=[NSMutableSet new];
    readable=true;
    for (NSString *directory in directories) {
        NSError *error=nil;
        NSArray<NSString *> *names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:directory error:&error];
        if (!names) {
            // Retired is optional until the OS archives a report. Losing the
            // active report directory (or access to an existing archive) is a
            // visibility gap even if the other directory remains readable.
            const bool absentArchive=[directory.lastPathComponent isEqualToString:@"Retired"] &&
                [error.domain isEqualToString:NSCocoaErrorDomain] && error.code==NSFileReadNoSuchFileError;
            if(!absentArchive)readable=false;
            continue;
        }
        for (NSString *name in names)
            if ([name.pathExtension isEqualToString:@"gpuRestart"])
                // Moving an existing report into Retired is not a new reset.
                [reports addObject:name];
    }
    return reports;
}
int main(int argc,char **argv) {
    @autoreleasepool {
        if (argc!=3) { fprintf(stderr,"usage: tgl-capture PRIVATE_DIRECTORY SECONDS(1..3600)\n"); return 2; }
        char *end=nullptr;
        long requested=strtol(argv[2],&end,10);
        if (!end || *end || requested<1 || requested>3600) return 2;
        int seconds=static_cast<int>(requested);
        NSString *directory=@(argv[1]); NSError *error=nil;
        if (![[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error] || chmod(directory.fileSystemRepresentation,0700)) return 3;
        io_registry_entry_t root=IORegistryGetRootEntry(kIOMainPortDefault); if (!root) return 4;
        signal(SIGINT,stop); signal(SIGTERM,stop);
        NSArray<NSString *> *reportDirectories=@[@"/Library/Logs/DiagnosticReports", @"/Library/Logs/DiagnosticReports/Retired"];
        bool reportsReadable=false;
        NSSet<NSString *> *knownReports=gpuReports(reportsReadable,reportDirectories);
        bool reportsContinuous=reportsReadable;
        NSMutableSet<NSString *> *newReports=[NSMutableSet new];
        const char *keys[]={"TGLRecoveryLive","TGLRecoverySinglePassReady","TGLHangFrozenState","TGLHangPreStop0","TGLHangBatch0","TGLHangBatch1","TGLHangBatch2","TGLHangBatch3","TGLHangPending0","TGLHangPendingBCS0","TGLHangPendingBCS1","TGLHangPendingBCS2","TGLHangPendingBCS3","TGLHangAddress0","TGLHangAddress1","TGLHangAddress2","TGLHangAddress3","TGLHangDetail0","TGLHangDetailStatus","TGLHangGGTT0","TGLHangGraph0","TGLHangRefs0","TGLSubmitHistory0","TGLSameTail0","TGLCSB12Live","TGLCSBFailure0","TGLGSYNC0","TGLRPCSPower0","TGLBlitDeferredInitLive","TGLSemaphoreTLBLive","TGLAcceleratorAdmissionReady","WEXTGLMTLLeakReady"};
        const char *bcsKeys[]={"TGLHangBCSWait0","TGLHangBCSWait1","TGLHangBCSWait2","TGLHangBCSWait3"};
        bool failedBefore=false;
        bool cleanBefore=false;
        auto stateBefore=xe::RecoveryState::Unknown;
        for (int elapsed=0;elapsed<=seconds && !stopped;elapsed+=2) {
            @autoreleasepool {
                NSMutableDictionary *snapshot=[NSMutableDictionary new];
                for (auto key:keys) {
                    CFStringRef name=CFStringCreateWithCString(nullptr,key,kCFStringEncodingUTF8);
                    CFTypeRef value=IORegistryEntryCreateCFProperty(root,name,kCFAllocatorDefault,0); CFRelease(name);
                    if (value) snapshot[@(key)]=CFBridgingRelease(value);
                }
                for (auto key:bcsKeys) {
                    CFStringRef name=CFStringCreateWithCString(nullptr,key,kCFStringEncodingUTF8);
                    CFTypeRef value=IORegistryEntryCreateCFProperty(root,name,kCFAllocatorDefault,0); CFRelease(name);
                    if (value) snapshot[@(key)]=CFBridgingRelease(value);
                }
                snapshot[@"capture_unix_seconds"]=@([NSDate date].timeIntervalSince1970);
                bool readable=false;
                NSSet<NSString *> *reports=gpuReports(readable,reportDirectories);
                const NSUInteger previousReports=newReports.count;
                if (reportsReadable && readable) {
                    for (NSString *report in reports)
                        if (![knownReports containsObject:report]) [newReports addObject:report];
                } else if (readable) {
                    // A previously unreadable directory cannot establish the
                    // time of an existing report; baseline it, do not alarm.
                    knownReports=reports;
                }
                reportsReadable=readable;
                reportsContinuous=reportsContinuous && readable;
                snapshot[@"gpu_report_monitor_available"]=@(readable);
                snapshot[@"gpu_report_monitor_continuous"]=@(reportsContinuous);
                snapshot[@"new_gpu_reports"]=newReports.allObjects;
                NSData *recovery=snapshot[@"TGLRecoveryLive"];
                xe::RecoverySnapshot state;
                if ([recovery isKindOfClass:[NSData class]])
                    state=xe::recoverySnapshot(recovery.bytes,recovery.length,
                        snapshot[@"TGLRecoverySinglePassReady"]==(__bridge id)kCFBooleanTrue);
                const auto calls=state.calls,returns=state.returns;
                const char *monitor=xe::recoveryStateName(state.state);
                snapshot[@"recovery_monitor_state"]=@(monitor);
                bool failed=calls>0 || returns>0 || newReports.count>0;
                for(unsigned i=0;i<4;++i) {
                    NSData *batch=snapshot[[NSString stringWithFormat:@"TGLHangBatch%u",i]];
                    failed=failed || ([batch isKindOfClass:[NSData class]] && batch.length>0);
                }
                const bool clean=xe::comparisonBaselineClean(state,reportsContinuous,failed);
                snapshot[@"comparison_baseline_clean"]=@(clean);
                // Periodic history survives reboot; fresh failure is persisted
                // immediately rather than waiting for the next periodic slot.
                if (elapsed%10==0 || failed!=failedBefore || clean!=cleanBefore ||
                    state.state!=stateBefore || newReports.count!=previousReports) {
                    NSData *plist=[NSPropertyListSerialization dataWithPropertyList:snapshot format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
                    NSString *file=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"capture-%06d.plist",elapsed]];
                    if (!plist || ![plist writeToFile:file options:NSDataWritingAtomic error:&error] || chmod(file.fileSystemRepresentation,0600)) { IOObjectRelease(root); return 5; }
                    printf("{\"elapsed\":%d,\"recovery_calls\":%llu,\"recovery_returns\":%llu,\"recovery_monitor\":\"%s\",\"comparison_baseline_clean\":%s,\"new_gpu_reports\":%lu,\"report_monitor_available\":%s,\"report_monitor_continuous\":%s,\"failure_data\":%s}\n",elapsed,calls,returns,monitor,clean?"true":"false",(unsigned long)newReports.count,readable?"true":"false",reportsContinuous?"true":"false",failed?"true":"false"); fflush(stdout);
                }
                failedBefore=failed;
                cleanBefore=clean;stateBefore=state.state;
            }
            if (elapsed<seconds && !stopped) sleep(2);
        }
        IOObjectRelease(root); return 0;
    }
}
