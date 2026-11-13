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
#include <fcntl.h>
#include <mach/mach_time.h>
#include "../src/RecoveryState.hpp"
#include "../driver/WhateverXe/WhateverXe/kern_tgl_hang_cpu.hpp"
static volatile sig_atomic_t stopped;
static void stop(int) { stopped=1; }
static id registryValue(io_registry_entry_t root,NSString *key) {
    return CFBridgingRelease(IORegistryEntryCreateCFProperty(root,
        (__bridge CFStringRef)key,kCFAllocatorDefault,0));
}
// Optional display observation: one standard shared client, immediately closed,
// every ten seconds. This asks the installed WEX to publish existing recorders;
// it calls no external method and never acquires WindowServer's type-0 client.
// Publication is not inert: the installed wrapper also runs its existing cursor
// observation. Record the open/close result; missing or stale data are not zeros.
static NSDictionary *publishDisplay() {
    io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,
        IOServiceMatching("AppleIntelFramebuffer"));
    if(!service)return @{@"service_available":@NO};
    io_connect_t client=IO_OBJECT_NULL;
    const auto opened=IOServiceOpen(service,mach_task_self(),1,&client);
    IOObjectRelease(service);
    NSMutableDictionary *result=[@{@"service_available":@YES,@"open_result":@(opened)} mutableCopy];
    if(opened==KERN_SUCCESS)result[@"close_result"]=@(IOServiceClose(client));
    return result;
}
static NSDictionary *probeSummary(id raw) {
    NSDictionary *probe=[raw isKindOfClass:[NSDictionary class]]?raw:nil;
    NSMutableDictionary *result=[@{@"kind":@"probe",@"unix_seconds":@([NSDate date].timeIntervalSince1970),
        @"probe_available":@(probe!=nil),@"probe":probe?:[NSNull null]} mutableCopy];
    for(NSString *key in @[@"NowNs",@"PipeAUnderruns",@"PipeALastNs",@"DonorUnderrunPipeA",@"DonorUnderrunEntry",
        @"Paused",@"SkippedDC",@"SkippedPW1Off",@"UnavailableAllOnes",@"UnavailablePCI",@"SkippedNoPowerLease",
        @"DonorCounterState",@"DonorCountersValid",@"MMIOAccessState",@"Writes"])
        result[key]=[probe[key] isKindOfClass:[NSNumber class]]?probe[key]:[NSNull null];
    return result;
}
static int displayCapture(io_registry_entry_t root,NSString *directory,int seconds,bool probeMode=false) {
    mach_timebase_info_data_t timebase{};mach_timebase_info(&timebase);
    uint64_t previousSequence=0;double previousTime=0;
    bool havePrevious=false;
    struct File { int fd=-1; ~File() { if(fd>=0)close(fd); } } probeLog;
    if(probeMode) {
        NSString *path=[directory stringByAppendingPathComponent:@"probe.ndjson"];
        probeLog.fd=open(path.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(probeLog.fd<0)return 5;
    }
    const int step=probeMode?1:10;
    for(int elapsed=0;elapsed<=seconds && !stopped;elapsed+=step) {
        @autoreleasepool {
            if(probeMode) {
                io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("TGLDisplayProbe"));
                id raw=service?registryValue(service,@"TGLDisplayProbe"):nil;
                if(service)IOObjectRelease(service);
                NSDictionary *sample=probeSummary(raw);
                NSData *json=[NSJSONSerialization dataWithJSONObject:sample options:NSJSONWritingSortedKeys error:nil];
                if(!json || write(probeLog.fd,json.bytes,json.length)!=(ssize_t)json.length || write(probeLog.fd,"\n",1)!=1)return 5;
                fwrite(json.bytes,1,json.length,stdout);putchar('\n');fflush(stdout);
                if([sample[@"Writes"] isKindOfClass:[NSNumber class]] && [sample[@"Writes"] unsignedLongLongValue]) {
                    fprintf(stderr,"Unexpected probe writes: stop the read-only observation.\n");return 8;
                }
            }
            if(elapsed%10==0) {
            NSMutableDictionary *snapshot=[NSMutableDictionary new];
            snapshot[@"publication"]=publishDisplay();
            const double now=[NSDate date].timeIntervalSince1970;
            const double uptime=double(mach_absolute_time())*timebase.numer/timebase.denom/1e9;
            snapshot[@"capture_unix_seconds"]=@(now);
            snapshot[@"capture_uptime_seconds"]=@(uptime);
            const NSArray *keys=@[@"TGLStreamErrorCount",@"TGLStreamAckCounts",@"TGLStreamErrorRing",
                @"TGLCCSAlignmentReady",@"TGLCCSAlignmentLive",@"TGLIOSurfaceCCSPolicyReady",
                @"TGLIOSurfaceCCSAtStart",@"TGLNoPOSHReady",@"TGLNoPOSHLive",@"TGLGpuRecReady",@"TGLGpuRecLive"];
            for(NSString *key in keys) {id value=registryValue(root,key);if(value)snapshot[key]=value;}
            NSMutableDictionary *summary=[@{@"unix_seconds":@(now),@"uptime_seconds":@(uptime),
                @"publication":snapshot[@"publication"],@"ack_bit31_count":snapshot[@"TGLStreamErrorCount"]?:[NSNull null],
                @"ack_records":[NSNull null],@"ack_counts":[NSNull null]} mutableCopy];
            NSData *ring=snapshot[@"TGLStreamErrorRing"];
            if([ring isKindOfClass:[NSData class]] && ring.length==64*16*sizeof(uint64_t)) {
                NSMutableArray *records=[NSMutableArray new];
                for(unsigned i=0;i<64;++i) {
                    uint64_t r[16];memcpy(r,(const char *)ring.bytes+i*sizeof(r),sizeof(r));
                    if(!r[2])continue;
                    // MEM1 and the ACK range identify the memory-only schema.
                    if(r[15]!=0x4D454D31 || r[3]<0x44408 || r[3]>0x44438 || (r[3]&15)!=8 || r[13]>1)continue;
                    [records addObject:@{@"sequence":@(r[2]),@"uptime_seconds":@(r[0]/1e9),
                        @"reg":[NSString stringWithFormat:@"0x%llx",r[3]],
                        @"ack_value":[NSString stringWithFormat:@"0x%08llx",r[4]],
                        @"kind":r[4]==0xFFFFFFFF?@"clear_all_write":@"bit31_ack_not_yet_hardware_confirmed"}];
                }
                [records sortUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"sequence" ascending:YES]]];
                summary[@"ack_records"]=records;
            }
            NSData *counts=snapshot[@"TGLStreamAckCounts"];
            if([counts isKindOfClass:[NSData class]] && counts.length==8*sizeof(uint32_t)) {
                uint32_t c[8];memcpy(c,counts.bytes,sizeof(c));
                NSMutableArray *array=[NSMutableArray new];for(auto v:c)[array addObject:@(v)];
                summary[@"ack_counts"]=array;
            }
            uint64_t maximum=0,latestNS=0;
            for(unsigned i=0;i<64;++i) {
                NSString *key=[NSString stringWithFormat:@"TGLScan2Last%u",i];
                NSData *data=registryValue(root,key);
                if(![data isKindOfClass:[NSData class]] || data.length!=256*sizeof(uint64_t))continue;
                uint64_t r[256];memcpy(r,data.bytes,sizeof(r));
                if(r[3]!=2 || r[4]>1 || r[255]!=0x4D454D31)continue;
                snapshot[key]=data;
                if(r[2]>maximum) {maximum=r[2];latestNS=r[0];}
            }
            summary[@"scan2_sequence"]=maximum?@(maximum):(id)[NSNull null];
            summary[@"scan2_latest_uptime_seconds"]=maximum?@(latestNS/1e9):(id)[NSNull null];
            if(maximum && havePrevious && maximum>=previousSequence && now>previousTime) {
                summary[@"scan2_records_per_second"]=@((maximum-previousSequence)/(now-previousTime));
                // Native updatePlane is observed before and after. This is an
                // approximate call rate, never a display refresh/flip rate.
                summary[@"approx_update_plane_calls_per_second"]=@((maximum-previousSequence)/(2*(now-previousTime)));
            }
            previousSequence=maximum;previousTime=now;havePrevious=maximum!=0;
            NSError *error=nil;
            NSData *plist=[NSPropertyListSerialization dataWithPropertyList:snapshot format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
            NSString *file=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"display-%06d.plist",elapsed]];
            if(!plist || ![plist writeToFile:file options:NSDataWritingAtomic error:&error] || chmod(file.fileSystemRepresentation,0600))return 5;
            NSData *json=[NSJSONSerialization dataWithJSONObject:summary options:NSJSONWritingSortedKeys error:&error];
            if(!json)return 6;
            fwrite(json.bytes,1,json.length,stdout);putchar('\n');fflush(stdout);
            }
        }
        // Short sleeps preserve SIGINT responsiveness. No observer runs here.
        for(int i=0;i<step && elapsed+i<seconds && !stopped;++i)sleep(1);
    }
    return 0;
}
struct HaltMonitor {
    const char *state="not_available";
    bool qualified=true,observed=false;
    uint32_t mask=0;
};
struct HaltHistory {
    bool seen=false,continuous=true,observed=false;
    void update(const HaltMonitor &monitor) {
        const bool available=strcmp(monitor.state,"not_available")!=0;
        continuous=continuous && monitor.qualified && (!seen || available);
        seen=seen || available;observed=observed || monitor.observed;
    }
    bool permitsComparison() const {return continuous && !observed;}
};
static HaltMonitor haltMonitor(id value,bool ready) {
    HaltMonitor result;
    if (!ready && !value) return result; // Older installed drivers lack this optional view.
    result.state="unknown";result.qualified=false;
    if (!ready || ![value isKindOfClass:[NSData class]]) return result;
    NSData *data=value;
    uint64_t header[8]{};
    if(data.length<sizeof(header))return result;
    memcpy(header,data.bytes,sizeof(header));
    if(header[0]!=TGLHangCPU::Schema || header[1]!=sizeof(TGLHangCPU::Capture))return result;
    size_t offset=sizeof(header);
    for(unsigned i=0;i<TGLHangCPU::Engines;++i) {
        if(header[i+2]>2)return result;
        if(header[i+2]) {result.observed=true;result.mask|=1U<<i;}
        if(header[i+2]!=2)continue;
        if(data.length-offset<sizeof(TGLHangCPU::Capture))return result;
        uint64_t q[12];memcpy(q,(const char *)data.bytes+offset,sizeof(q));
        if(q[0]!=TGLHangCPU::Schema || q[1]>4 || q[7]!=i || q[10]>2)return result;
        offset+=sizeof(TGLHangCPU::Capture);
    }
    if(offset!=data.length)return result;
    result.qualified=true;result.state=result.observed?"observed":"clear";
    return result;
}
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
        const bool probe=argc==4 && strcmp(argv[1],"--probe")==0;
        const bool display=probe || (argc==4 && strcmp(argv[1],"--display")==0);
        if (argc!=3 && !display) { fprintf(stderr,"usage: tgl-capture [--display|--probe] PRIVATE_DIRECTORY SECONDS(1..3600; display: multiples of 10)\n"); return 2; }
        const int first=display?2:1;
        char *end=nullptr;
        long requested=strtol(argv[first+1],&end,10);
        if (!end || *end || requested<1 || requested>3600) return 2;
        if(display && !probe && requested%10)return 2;
        int seconds=static_cast<int>(requested);
        NSString *directory=@(argv[first]); NSError *error=nil;
        if (![[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error] || chmod(directory.fileSystemRepresentation,0700)) return 3;
        io_registry_entry_t root=IORegistryGetRootEntry(kIOMainPortDefault); if (!root) return 4;
        signal(SIGINT,stop); signal(SIGTERM,stop);
        if(display) {const int result=displayCapture(root,directory,seconds,probe);IOObjectRelease(root);return result;}
        NSArray<NSString *> *reportDirectories=@[@"/Library/Logs/DiagnosticReports", @"/Library/Logs/DiagnosticReports/Retired"];
        bool reportsReadable=false;
        NSSet<NSString *> *knownReports=gpuReports(reportsReadable,reportDirectories);
        bool reportsContinuous=reportsReadable;
        NSMutableSet<NSString *> *newReports=[NSMutableSet new];
        const char *keys[]={"TGLRecoveryLive","TGLRecoverySinglePassReady","TGLHangFrozenState","TGLHangPreStop0","TGLHangBatch0","TGLHangBatch1","TGLHangBatch2","TGLHangBatch3","TGLHangPending0","TGLHangPendingBCS0","TGLHangPendingBCS1","TGLHangPendingBCS2","TGLHangPendingBCS3","TGLHangAddress0","TGLHangAddress1","TGLHangAddress2","TGLHangAddress3","TGLHangDetail0","TGLHangDetailStatus","TGLHangGGTT0","TGLHangGraph0","TGLHangRefs0","TGLSubmitHistory0","TGLSameTail0","TGLCSB12Live","TGLCSBFailure0","TGLGSYNC0","TGLRPCSPower0","TGLBlitDeferredInitLive","TGLSemaphoreTLBLive","TGLAcceleratorAdmissionReady","WEXTGLMTLLeakReady"};
        const char *bcsKeys[]={"TGLHangBCSWait0","TGLHangBCSWait1","TGLHangBCSWait2","TGLHangBCSWait3"};
        bool failedBefore=false;
        bool cleanBefore=false;
        HaltMonitor haltBefore;
        NSData *haltDataBefore=nil;
        HaltHistory haltHistory;
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
                const CFStringRef haltKeys[]{CFSTR("TGLHangCPUReady"),CFSTR("TGLHangCPULive")};
                for (CFStringRef key:haltKeys) {
                    CFTypeRef value=IORegistryEntryCreateCFProperty(root,key,kCFAllocatorDefault,0);
                    if(value)snapshot[(__bridge NSString *)key]=CFBridgingRelease(value);
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
                const auto halt=haltMonitor(snapshot[@"TGLHangCPULive"],
                    snapshot[@"TGLHangCPUReady"]==(__bridge id)kCFBooleanTrue);
                NSData *haltData=[snapshot[@"TGLHangCPULive"] isKindOfClass:[NSData class]]?
                    snapshot[@"TGLHangCPULive"]:nil;
                const bool haltChanged=(haltData || haltDataBefore) && ![haltData isEqual:haltDataBefore];
                haltHistory.update(halt);
                snapshot[@"halt_monitor_state"]=@(halt.state);
                snapshot[@"halt_observed_engine_mask"]=@(halt.mask);
                snapshot[@"halt_monitor_continuous"]=@(haltHistory.continuous);
                // A halt invocation is a timeout witness, not proof of its
                // initial cause. Even an incomplete record rejects a clean A/B.
                const bool clean=xe::comparisonBaselineClean(state,reportsContinuous,
                    failed || !haltHistory.permitsComparison());
                snapshot[@"comparison_baseline_clean"]=@(clean);
                // Periodic history survives reboot; fresh failure is persisted
                // immediately rather than waiting for the next periodic slot.
                if (elapsed%10==0 || failed!=failedBefore || clean!=cleanBefore ||
                    state.state!=stateBefore || newReports.count!=previousReports ||
                    strcmp(halt.state,haltBefore.state) || halt.mask!=haltBefore.mask || haltChanged) {
                    NSData *plist=[NSPropertyListSerialization dataWithPropertyList:snapshot format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
                    NSString *file=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"capture-%06d.plist",elapsed]];
                    if (!plist || ![plist writeToFile:file options:NSDataWritingAtomic error:&error] || chmod(file.fileSystemRepresentation,0600)) { IOObjectRelease(root); return 5; }
                    printf("{\"elapsed\":%d,\"recovery_calls\":%llu,\"recovery_returns\":%llu,\"recovery_monitor\":\"%s\",\"comparison_baseline_clean\":%s,\"new_gpu_reports\":%lu,\"report_monitor_available\":%s,\"report_monitor_continuous\":%s,\"failure_data\":%s,\"halt_monitor\":\"%s\",\"halt_engine_mask\":%u}\n",elapsed,calls,returns,monitor,clean?"true":"false",(unsigned long)newReports.count,readable?"true":"false",reportsContinuous?"true":"false",failed?"true":"false",halt.state,halt.mask); fflush(stdout);
                }
                failedBefore=failed;
                cleanBefore=clean;stateBefore=state.state;
                haltBefore=halt;
                haltDataBefore=haltData;
            }
            if (elapsed<seconds && !stopped) sleep(2);
        }
        IOObjectRelease(root); return 0;
    }
}
