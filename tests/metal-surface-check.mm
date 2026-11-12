// SPDX-License-Identifier: MIT
// Bounded CVMetalTextureCache/IOSurface rendering, optionally presented in this
// diagnostic's own window. Readback validates render output, not panel scanout.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include <malloc/malloc.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/resource.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include "../src/BoundedCount.hpp"
#include "../src/POSHQueueCache.h"

static uint32_t pattern(unsigned x,unsigned y,unsigned frame) {
    const unsigned b=(x*3+frame*7)&255, g=(y*5+frame*11)&255;
    const unsigned r=((y<90?0x25:0xd1)+frame*13)&255;
    return 0xff000000|(r<<16)|(g<<8)|b;
}
static bool noNewGPUReport(NSSet<NSString *> *baseline) {
    NSArray *names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:@"/Library/Logs/DiagnosticReports" error:nil];
    if (!names) return false;
    for (NSString *name in names)
        if ([name.pathExtension isEqualToString:@"gpuRestart"] && ![baseline containsObject:name]) return false;
    return true;
}
static size_t memory() {
    malloc_statistics_t stats{}; malloc_zone_statistics(nullptr,&stats); return stats.size_in_use;
}
struct Presentation {
    std::array<double,600> times{};
    std::atomic<unsigned> callbacks{0},valid{0};
    dispatch_group_t pending=dispatch_group_create();
};
static void pumpEvents() {
    for (unsigned n=0;n<16;++n) {
        NSEvent *event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        if (!event) break;
        [NSApp sendEvent:event];
    }
    [NSApp updateWindows];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.0001]];
}
static bool temperatureOK(NSString *path) {
    if (!path) return true;
    NSDictionary *attributes=[[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    NSDate *modified=attributes[NSFileModificationDate];
    NSString *log=[NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    NSRange range=[log rangeOfString:@"CPU die temperature: " options:NSBackwardsSearch];
    double degrees=0;
    if (!modified || -modified.timeIntervalSinceNow>6 || !log || range.location==NSNotFound ||
        sscanf([log substringFromIndex:range.location].UTF8String,"CPU die temperature: %lf",&degrees)!=1 ||
        degrees<=0 || degrees>=75) {
        fprintf(stderr,"Stop: temperature sample unavailable/stale or >=75 C\n");
        return false;
    }
    return true;
}
int main(int argc,char **argv) {
    @autoreleasepool {
        int count=60;
        const bool candidate=argc>1 && !strcmp(argv[1],"queue-cache");
        if ((argc>1 && !candidate && strcmp(argv[1],"stock")) ||
            (argc>2 && !xe::boundedCount(argv[2],600,count))) return 2;
        NSString *powerLog=nil; bool present=false;
        for (int n=3;n<argc;++n) {
            if (!strcmp(argv[n],"--present") && !present) present=true;
            else if (!strcmp(argv[n],"--power-log") && !powerLog && n+1<argc) powerLog=@(argv[++n]);
            else return 2;
        }
        if (!temperatureOK(powerLog)) return 14;
        NSArray *names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:@"/Library/Logs/DiagnosticReports" error:nil];
        if (!names) return 4;
        NSSet<NSString *> *baseline=[NSSet setWithArray:names];
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); if (!device) return 3;
        if (candidate && !installPOSHQueueCache(device.registryID)) return 4;
        id<MTLCommandQueue> queue=[device newCommandQueue]; if (!queue) return 5;
        NSError *error=nil;
        NSString *shader=@"#include <metal_stdlib>\nusing namespace metal;\n"
          "struct V { float4 p [[position]]; float2 uv; };\n"
          "vertex V triangle(uint i [[vertex_id]]) { const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; V v; v.p=float4(p[i],0,1); v.uv=p[i]*float2(.5,-.5)+.5; return v; }\n"
          "fragment float4 samplePixel(V v [[stage_in]],texture2d<float> input [[texture(0)]]) { constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::nearest); return input.sample(s,v.uv); }\n";
        id<MTLLibrary> library=[device newLibraryWithSource:shader options:nil error:&error];
        if (!library) { fprintf(stderr,"shader: %s\n",error.description.UTF8String); return 6; }
        MTLRenderPipelineDescriptor *pipeline=[MTLRenderPipelineDescriptor new];
        pipeline.vertexFunction=[library newFunctionWithName:@"triangle"];
        pipeline.fragmentFunction=[library newFunctionWithName:@"samplePixel"];
        pipeline.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
        id<MTLRenderPipelineState> state=[device newRenderPipelineStateWithDescriptor:pipeline error:&error];
        if (!state) return 6;
        NSWindow *window=nil; CAMetalLayer *layer=nil;
        auto presentation=std::make_shared<Presentation>();
        bool presentationDrained=true;
        if (present) {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            [NSApp finishLaunching];
            window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,640,360)
                styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
            window.title=@"WhateverXe bounded presentation test";
            window.releasedWhenClosed=NO;
            layer=[CAMetalLayer layer]; layer.device=device;
            layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
            layer.frame=window.contentView.bounds;
            layer.drawableSize=CGSizeMake(640,360);
            layer.framebufferOnly=YES; layer.displaySyncEnabled=YES;
            layer.maximumDrawableCount=3; layer.allowsNextDrawableTimeout=YES;
            window.contentView.wantsLayer=YES; window.contentView.layer=layer;
            [window orderFront:nil]; [window display]; [CATransaction flush]; pumpEvents();
            printf("{\"window_visible\":%s,\"layer_width\":%.0f,\"layer_height\":%.0f}\n",
                window.visible?"true":"false",layer.bounds.size.width,layer.bounds.size.height);
        }
        CVMetalTextureCacheRef cache=nullptr;
        if (CVMetalTextureCacheCreate(nullptr,nullptr,device,nullptr,&cache)!=kCVReturnSuccess) return 7;
        NSDictionary *attributes=@{(id)kCVPixelBufferWidthKey:@320,(id)kCVPixelBufferHeightKey:@180,
          (id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),
          (id)kCVPixelBufferIOSurfacePropertiesKey:@{},(id)kCVPixelBufferMetalCompatibilityKey:@YES};
        CVPixelBufferPoolRef pool=nullptr;
        if (CVPixelBufferPoolCreate(nullptr,nullptr,(__bridge CFDictionaryRef)attributes,&pool)!=kCVReturnSuccess) { CFRelease(cache); return 7; }
        std::vector<double> frameTimes; frameTimes.reserve(count);
        const size_t before=memory();
        const double startUnix=[NSDate date].timeIntervalSince1970;
        const auto started=std::chrono::steady_clock::now();
        rusage cpuBefore{}; getrusage(RUSAGE_SELF,&cpuBefore);
        unsigned completed=0,mismatches=0;
        int result=0;
        for (int n=0;n<count && !result;++n) {
            const auto frameStarted=std::chrono::steady_clock::now();
            if (n%15==0 && !temperatureOK(powerLog)) { result=14; break; }
            @autoreleasepool {
                id<CAMetalDrawable> drawable=nil;
                if (present) {
                    pumpEvents(); drawable=[layer nextDrawable];
                    if (!drawable) { result=15; break; }
                }
                CVPixelBufferRef source=nullptr,target=nullptr;
                CVMetalTextureRef input=nullptr,output=nullptr;
                // CF holders also release on every early return in this scope.
                struct Cleanup {
                    CVPixelBufferRef &s,&t; CVMetalTextureRef &i,&o;
                    ~Cleanup() { if(i)CFRelease(i);if(o)CFRelease(o);if(s)CFRelease(s);if(t)CFRelease(t); }
                } cleanup{source,target,input,output};
                if (CVPixelBufferPoolCreatePixelBuffer(nullptr,pool,&source)!=kCVReturnSuccess ||
                    CVPixelBufferPoolCreatePixelBuffer(nullptr,pool,&target)!=kCVReturnSuccess) { result=7; break; }
                if (CVPixelBufferLockBaseAddress(source,0)!=kCVReturnSuccess) { result=7; break; }
                for (unsigned y=0;y<180;++y) {
                    auto row=(uint32_t *)((char *)CVPixelBufferGetBaseAddress(source)+y*CVPixelBufferGetBytesPerRow(source));
                    for (unsigned x=0;x<320;++x) row[x]=pattern(x,y,n);
                }
                CVPixelBufferUnlockBaseAddress(source,0);
                if (CVMetalTextureCacheCreateTextureFromImage(nullptr,cache,source,nullptr,MTLPixelFormatBGRA8Unorm,320,180,0,&input)!=kCVReturnSuccess ||
                    CVMetalTextureCacheCreateTextureFromImage(nullptr,cache,target,nullptr,MTLPixelFormatBGRA8Unorm,320,180,0,&output)!=kCVReturnSuccess) { result=7; break; }
                id<MTLTexture> inTexture=CVMetalTextureGetTexture(input),outTexture=CVMetalTextureGetTexture(output);
                if (!inTexture || !outTexture) { result=7; break; }
                MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture=outTexture;
                pass.colorAttachments[0].loadAction=MTLLoadActionClear;
                pass.colorAttachments[0].storeAction=MTLStoreActionStore;
                id<MTLCommandBuffer> cb=[queue commandBuffer];
                id<MTLRenderCommandEncoder> encoder=[cb renderCommandEncoderWithDescriptor:pass];
                if (!cb || !encoder) { result=5; break; }
                [encoder setRenderPipelineState:state];
                [encoder setFragmentTexture:inTexture atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                [encoder endEncoding];
                if (present) {
                    // Exercise render-to-texture -> sampling -> CAMetalLayer,
                    // then the same unchanged native present/commit path as a
                    // streaming client. This does not hook WindowServer.
                    MTLRenderPassDescriptor *display=[MTLRenderPassDescriptor renderPassDescriptor];
                    display.colorAttachments[0].texture=drawable.texture;
                    display.colorAttachments[0].loadAction=MTLLoadActionClear;
                    display.colorAttachments[0].storeAction=MTLStoreActionStore;
                    id<MTLRenderCommandEncoder> show=[cb renderCommandEncoderWithDescriptor:display];
                    if (!show) { result=5; break; }
                    [show setRenderPipelineState:state];
                    [show setFragmentTexture:outTexture atIndex:0];
                    [show drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    [show endEncoding];
                    dispatch_group_enter(presentation->pending);
                    [drawable addPresentedHandler:^(id<MTLDrawable> shown) {
                        presentation->times[n]=shown.presentedTime;
                        if (shown.presentedTime>0) ++presentation->valid;
                        ++presentation->callbacks;
                        dispatch_group_leave(presentation->pending);
                    }];
                    [cb presentDrawable:drawable];
                }
                dispatch_semaphore_t done=dispatch_semaphore_create(0);
                [cb addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(done); }];
                [cb commit];
                if (dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC))) { result=12; break; }
                if (cb.status!=MTLCommandBufferStatusCompleted) { fprintf(stderr,"GPU: %s\n",cb.error.description.UTF8String); result=8; break; }
                if (CVPixelBufferLockBaseAddress(target,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess) { result=7; break; }
                for (unsigned y=0;y<180;++y) {
                    const auto row=(const uint32_t *)((const char *)CVPixelBufferGetBaseAddress(target)+y*CVPixelBufferGetBytesPerRow(target));
                    for (unsigned x=0;x<320;++x) if (row[x]!=pattern(x,y,n)) ++mismatches;
                }
                CVPixelBufferUnlockBaseAddress(target,kCVPixelBufferLock_ReadOnly);
                ++completed;
                if (mismatches) { result=9; break; }
                // Repeatedly flush texture wrappers while the pixel-buffer pool
                // recycles IOSurfaces. Work is paced, with no visible rendering.
                CVMetalTextureCacheFlush(cache,0);
                if (n%15==14 && !noNewGPUReport(baseline)) { result=13; break; }
                if (n%60==59) {
                    printf("{\"progress\":%d,\"malloc_bytes\":%zu}\n",n+1,memory()); fflush(stdout);
                }
            }
            frameTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStarted).count());
            usleep(16667);
        }
        if (present) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while (dispatch_group_wait(presentation->pending,DISPATCH_TIME_NOW) &&
                   std::chrono::steady_clock::now()<deadline) pumpEvents();
            presentationDrained=dispatch_group_wait(presentation->pending,DISPATCH_TIME_NOW)==0;
            if (!presentationDrained ||
                presentation->valid!=completed) result=result?result:16;
            [window orderOut:nil];
        }
        const size_t after=memory();
        rusage cpuAfter{}; getrusage(RUSAGE_SELF,&cpuAfter);
        auto time=[](timeval v) { return v.tv_sec+v.tv_usec/1e6; };
        const double cpuSeconds=time(cpuAfter.ru_utime)+time(cpuAfter.ru_stime)-time(cpuBefore.ru_utime)-time(cpuBefore.ru_stime);
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::sort(frameTimes.begin(),frameTimes.end());
        auto percentile=[&](double p) { return frameTimes.empty()?0:frameTimes[static_cast<size_t>((frameTimes.size()-1)*p)]; };
        const auto stats=poshQueueCacheStats();
        if (present) {
            std::vector<double> intervals;
            for (unsigned n=1;presentationDrained && n<completed;++n)
                if (presentation->times[n]>0 && presentation->times[n-1]>0)
                    intervals.push_back((presentation->times[n]-presentation->times[n-1])*1000);
            std::sort(intervals.begin(),intervals.end());
            const double p95=intervals.empty()?0:intervals[(intervals.size()-1)*95/100];
            printf("{\"presentation\":true,\"present_callbacks\":%u,\"valid_present_times\":%u,\"callbacks_drained\":%s,\"intervals\":%zu,\"present_interval_p95_ms\":%.4f,\"panel_scanout_verified\":false}\n",
                presentation->callbacks.load(),presentation->valid.load(),presentationDrained?"true":"false",intervals.size(),p95);
        }
        printf("{\"mode\":\"%s\",\"frames\":%u,\"pixel_mismatches\":%u,\"malloc_before\":%zu,\"malloc_after\":%zu,\"allocated\":%llu,\"refused\":%llu,\"new_gpu_report\":%s,\"result\":%d,\"start_unix\":%.3f,\"end_unix\":%.3f,\"elapsed_seconds\":%.6f,\"cpu_seconds\":%.6f,\"frame_work_p50_ms\":%.4f,\"frame_work_p95_ms\":%.4f}\n",
            candidate?"queue-cache":"stock",completed,mismatches,before,after,stats.allocated,stats.refused,noNewGPUReport(baseline)?"false":"true",result,
            startUnix,[NSDate date].timeIntervalSince1970,elapsed,cpuSeconds,percentile(.5),percentile(.95));
        CFRelease(pool); CFRelease(cache);
        if (candidate && (!stats.allocated || stats.refused)) return 10;
        return result;
    }
}
