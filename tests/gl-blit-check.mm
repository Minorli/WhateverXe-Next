// SPDX-License-Identifier: MIT
// The observed panic stack uses IGAccelGLContext's 2D blit path. This bounded
// offscreen probe targets that API family, not a claim of exact call coverage.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <OpenGL/gl3.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include "../src/POSHDeferred.h"
#include "../src/BoundedCount.hpp"

int main(int argc,char **argv) {
    @autoreleasepool {
        int count=500;
        if (argc>3 || (argc>1 && !xe::boundedCount(argv[1],10000,count))) return 2;
        bool mixed=argc>2 && strcmp(argv[2],"mixed")==0;
        if(count<1 || count>10000 || (argc>2 && !mixed))return 2;
        NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAOpenGLProfile,NSOpenGLProfileVersion3_2Core,NSOpenGLPFAAccelerated,NSOpenGLPFAColorSize,24,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
        NSOpenGLContext *context=[[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
        if(!context)return 3;[context makeCurrentContext];
        const auto renderer=(const char *)glGetString(GL_RENDERER);
        if(!renderer || !strstr(renderer,"Intel"))return 3;
        printf("{\"renderer\":\"%s\",\"gl_version\":\"%s\",\"mixed\":%s}\n",renderer,glGetString(GL_VERSION),mixed?"true":"false");fflush(stdout);
        id<MTLDevice> device=mixed?MTLCreateSystemDefaultDevice():nil;
        id<MTLCommandQueue> queue=mixed?[device newCommandQueue]:nil;
        if(mixed && (!queue || !installPOSHDeferredFix()))return 4;
        std::atomic<bool> stop{false}; std::atomic<unsigned> metalDone{0},metalErrors{0};
        // Metal runs in this diagnostic only. No changes to GL code or other
        // processes; the deferred hook avoids an unrelated synthetic POSH leak.
        std::thread worker;
        if(mixed)worker=std::thread([&]{@autoreleasepool {
            auto source=[device newBufferWithLength:1048576 options:MTLResourceStorageModeShared];
            auto target=[device newBufferWithLength:1048576 options:MTLResourceStorageModeShared];
            if(!source || !target){++metalErrors;return;}memset(source.contents,0x5a,1048576);
            while(!stop && metalDone<10000){@autoreleasepool {
                auto cb=[queue commandBuffer];auto e=[cb blitCommandEncoder];
                if(!cb || !e){++metalErrors;break;}
                [e copyFromBuffer:source sourceOffset:0 toBuffer:target destinationOffset:0 size:1048576];[e endEncoding];[cb commit];[cb waitUntilCompleted];
                if(cb.status!=MTLCommandBufferStatusCompleted || memcmp(source.contents,target.contents,1048576)){++metalErrors;break;}++metalDone;
            }}
        }});
        GLuint textures[2]{},fb[2]{};glGenTextures(2,textures);glGenFramebuffers(2,fb);
        constexpr int width=512,height=512;
        for(int i=0;i<2;++i){glBindTexture(GL_TEXTURE_2D,textures[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glBindFramebuffer(GL_FRAMEBUFFER,fb[i]);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);}
        unsigned bad=0,errors=0,completed=0;auto begin=std::chrono::steady_clock::now();
        for(int n=0;n<count;++n){
            glBindFramebuffer(GL_FRAMEBUFFER,fb[0]);glViewport(0,0,width,height);
            const unsigned char r=(n*19+7)&255,g=(n*31+13)&255,b=(n*7+29)&255;
            glClearColor(r/255.0f,g/255.0f,b/255.0f,1);glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,fb[0]);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fb[1]);
            glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,fb[1]);glBindTexture(GL_TEXTURE_2D,textures[0]);
            glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
            if((n+1)%16==0 || n+1==count){
                GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
                auto wait=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,5000000000ULL);glDeleteSync(fence);
                if(wait!=GL_ALREADY_SIGNALED && wait!=GL_CONDITION_SATISFIED){++errors;break;}
                glBindFramebuffer(GL_READ_FRAMEBUFFER,fb[0]);unsigned char pixel[4]{};glReadPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(abs((int)pixel[0]-r)>1 || abs((int)pixel[1]-g)>1 || abs((int)pixel[2]-b)>1 || pixel[3]!=255)++bad;
                if(glGetError()!=GL_NO_ERROR){++errors;break;}
            }
            ++completed;
        }
        stop=true;if(worker.joinable())worker.join();
        glDeleteFramebuffers(2,fb);glDeleteTextures(2,textures);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
        printf("{\"requested\":%d,\"completed\":%u,\"pixel_mismatches\":%u,\"gl_errors\":%u,\"metal_completed\":%u,\"metal_errors\":%u,\"seconds\":%.3f}\n",count,completed,bad,errors,metalDone.load(),metalErrors.load(),seconds);
        return bad || errors || metalErrors || completed!=(unsigned)count || (mixed && !metalDone)?7:0;
    }
}
