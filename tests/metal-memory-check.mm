// SPDX-License-Identifier: MIT
// Process-local research probe; see docs/POSH.md before enabling deferred mode.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "../src/POSHDeferred.h"
#include "../src/POSHQueueCache.h"
#include "../src/BoundedCount.hpp"

static void report(const char *phase, int count) {
    task_vm_info_data_t info{};
    mach_msg_type_number_t size = TASK_VM_INFO_COUNT;
    task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &size);
    malloc_statistics_t stats{};
    malloc_zone_statistics(nullptr, &stats);
    printf("{\"phase\":\"%s\",\"iterations\":%d,\"resident_bytes\":%llu,\"footprint_bytes\":%llu,\"malloc_bytes\":%zu}\n",
           phase, count, info.resident_size, info.phys_footprint, stats.size_in_use);
    fflush(stdout);
}

int main(int argc, char **argv) {
    @autoreleasepool {
        const char *mode = argc > 1 ? argv[1] : "empty";
        int count = 200;
        if (argc > 2 && !xe::boundedCount(argv[2],12000,count)) return 2;
        bool deferred = argc > 3 && strcmp(argv[3], "deferred") == 0;
        bool queueCache = argc > 3 && strcmp(argv[3], "queue-cache") == 0;
        if (argc > 4 || count < 1 || count > 12000 ||
            (argc > 3 && !deferred && !queueCache && strcmp(argv[3], "stock") != 0)) return 2;
        bool blit = strcmp(mode, "blit") == 0;
        bool drawOverlap = strcmp(mode, "draw-overlap") == 0;
        bool draw = strcmp(mode, "draw") == 0 || drawOverlap;
        bool compute = strcmp(mode, "compute") == 0;
        bool render = strcmp(mode, "render") == 0 || draw;
        bool overlap = strcmp(mode, "overlap") == 0 || drawOverlap;
        if (!blit && !render && !overlap && !compute && strcmp(mode, "empty") != 0) return 2;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 3;
        if (queueCache && !installPOSHQueueCache(device.registryID)) { fprintf(stderr,"Queue-cache guard refused\n"); return 4; }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) { fprintf(stderr, "Command queue creation failed\n"); return 8; }
        printf("{\"queue_class\":\"%s\"}\n", class_getName(object_getClass(queue)));
        if (deferred && !installPOSHDeferredFix()) { fprintf(stderr,"Deferred guard refused\n"); return 4; }
        const NSUInteger length = 1024 * 1024;
        id<MTLBuffer> source, target;
        id<MTLTexture> texture;
        id<MTLComputePipelineState> computeState;
        id<MTLRenderPipelineState> renderState;
        if (compute || draw) {
            NSError *error = nil;
            NSString *shader = @"#include <metal_stdlib>\nusing namespace metal;\n"
              "kernel void addone(device const float *a [[buffer(0)]], device float *b [[buffer(1)]], uint i [[thread_position_in_grid]]) { b[i]=a[i]+1.0f; }\n"
              "vertex float4 triangle(uint i [[vertex_id]]) { const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[i],0,1); }\n"
              "fragment float4 color() { return float4(0.25,0.5,0.75,1); }\n";
            id<MTLLibrary> library = [device newLibraryWithSource:shader options:nil error:&error];
            if (!library) { fprintf(stderr, "Shader compilation failed: %s\n", error.description.UTF8String); return 9; }
            if (compute) {
                computeState = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"addone"] error:&error];
                if (!computeState) { fprintf(stderr, "Compute pipeline failed: %s\n", error.description.UTF8String); return 9; }
            } else {
                MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
                desc.vertexFunction = [library newFunctionWithName:@"triangle"];
                desc.fragmentFunction = [library newFunctionWithName:@"color"];
                desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
                renderState = [device newRenderPipelineStateWithDescriptor:desc error:&error];
                if (!renderState) { fprintf(stderr, "Render pipeline failed: %s\n", error.description.UTF8String); return 9; }
            }
        }
        if (blit || compute) {
            source = [device newBufferWithLength:length options:MTLResourceStorageModeShared];
            target = [device newBufferWithLength:length options:MTLResourceStorageModeShared];
            if (!source || !target) return 5;
            if (compute) for (NSUInteger i = 0; i < length / sizeof(float); ++i) static_cast<float *>(source.contents)[i] = i * 0.25f;
            else memset(source.contents, 0x5a, length);
            memset(target.contents, 0, length);
        }
        if (render) {
            MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:128 height:128 mipmapped:NO];
            desc.usage = MTLTextureUsageRenderTarget;
            desc.storageMode = MTLStorageModeShared;
            texture = [device newTextureWithDescriptor:desc];
            if (!texture) return 5;
        }
        printf("{\"device\":\"%s\",\"mode\":\"%s\",\"count\":%d}\n", device.name.UTF8String, mode, count);
        auto begin = std::chrono::steady_clock::now();
        report("start", 0);
        for (int i = 1; i <= count; ++i) {
            @autoreleasepool {
                const int batch = overlap ? 4 : 1;
                id<MTLCommandBuffer> buffers[4];
                for (int j = 0; j < batch; ++j) {
                    buffers[j] = [queue commandBuffer];
                    if (!buffers[j]) { fprintf(stderr, "Command buffer creation failed in loop\n"); return 8; }
                    if (i == 1 && j == 0) {
                        printf("{\"buffer_class\":\"%s\"}\n", class_getName(object_getClass(buffers[j])));
                    }
                    if (blit) {
                        id<MTLBlitCommandEncoder> encoder = [buffers[j] blitCommandEncoder];
                        [encoder copyFromBuffer:source sourceOffset:0 toBuffer:target destinationOffset:0 size:length];
                        [encoder endEncoding];
                    } else if (compute) {
                        id<MTLComputeCommandEncoder> encoder = [buffers[j] computeCommandEncoder];
                        [encoder setComputePipelineState:computeState];
                        [encoder setBuffer:source offset:0 atIndex:0];
                        [encoder setBuffer:target offset:0 atIndex:1];
                        [encoder dispatchThreadgroups:MTLSizeMake(length / sizeof(float) / 256, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                        [encoder endEncoding];
                    } else if (render) {
                        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
                        pass.colorAttachments[0].texture = texture;
                        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                        pass.colorAttachments[0].clearColor = draw ? MTLClearColorMake(0, 0, 0, 0) : MTLClearColorMake(0.25, 0.5, 0.75, 1);
                        id<MTLRenderCommandEncoder> encoder = [buffers[j] renderCommandEncoderWithDescriptor:pass];
                        if (draw) { [encoder setRenderPipelineState:renderState]; [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; }
                        [encoder endEncoding];
                    }
                }
                for (int j = 0; j < batch; ++j) [buffers[j] commit];
                for (int j = 0; j < batch; ++j) {
                    [buffers[j] waitUntilCompleted];
                    if (buffers[j].status != MTLCommandBufferStatusCompleted) {
                        fprintf(stderr, "Command buffer failed: %s\n", buffers[j].error.description.UTF8String);
                        return 6;
                    }
                    buffers[j] = nil;
                }
            }
            if (i % 500 == 0 || i == count) report("progress", i);
        }
        bool valid = true;
        if (blit) valid = memcmp(source.contents, target.contents, length) == 0;
        if (compute) {
            const float *input = static_cast<const float *>(source.contents);
            const float *output = static_cast<const float *>(target.contents);
            for (NSUInteger i = 0; i < length / sizeof(float); ++i) if (output[i] != input[i] + 1.0f) { valid = false; break; }
        }
        if (render) {
            unsigned char pixel[4]{};
            [texture getBytes:pixel bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
            valid = abs((int)pixel[0] - 64) <= 1 && abs((int)pixel[1] - 128) <= 1 && abs((int)pixel[2] - 191) <= 1 && pixel[3] == 255;
            printf("{\"pixel\":[%u,%u,%u,%u]}\n",pixel[0],pixel[1],pixel[2],pixel[3]);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        printf("{\"completed\":true,\"data_valid\":%s,\"seconds\":%.3f}\n", valid ? "true" : "false", seconds);
        if (deferred) {
            const auto s=poshDeferredStats();
            printf("{\"deferred\":true,\"tracked\":%llu,\"detached\":%llu,\"disposed\":%llu,\"rejected\":%llu,\"completions\":%llu}\n",
              s.tracked,s.detached,s.disposed,s.rejected,s.completions);
            if (s.rejected || !s.tracked || s.detached!=s.disposed) return 10;
        }
        if (queueCache) {
            const auto s=poshQueueCacheStats();
            printf("{\"queue_cache\":true,\"allocated\":%llu,\"destroyed\":%llu,\"refused\":%llu}\n",s.allocated,s.destroyed,s.refused);
            if (!s.allocated || s.refused) return 10;
        }
        return valid ? 0 : 7;
    }
}
