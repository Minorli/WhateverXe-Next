// SPDX-License-Identifier: MIT
// Exercise the production filesystem observer. The renamed capture entry point
// is never called: no IOKit access, GPU creation or real report directories.
#define main captureMainNotInvoked
#include "../tools/tgl-capture.mm"
#undef main
#include <cassert>
int main() {
    @autoreleasepool {
        // Production parser: an optional older driver is different from a
        // ready monitor with missing/truncated evidence. No registry access.
        assert(haltMonitor(nil,false).qualified && !haltMonitor(nil,false).observed);
        assert(!haltMonitor(nil,true).qualified);
        uint64_t header[8]{TGLHangCPU::Schema,sizeof(TGLHangCPU::Capture)};
        auto payload=[&] {return [NSMutableData dataWithBytes:header length:sizeof(header)];};
        assert(haltMonitor(payload(),true).qualified && !haltMonitor(payload(),true).observed);
        assert(!haltMonitor(payload(),false).qualified);
        HaltHistory history;
        history.update(haltMonitor(nil,false));assert(history.permitsComparison() && !history.seen);
        history.update(haltMonitor(payload(),true));assert(history.permitsComparison() && history.seen);
        history.update(haltMonitor(nil,false));assert(!history.permitsComparison());
        history.update(haltMonitor(payload(),true));assert(!history.permitsComparison());
        history={};history.update(haltMonitor(nil,true));
        history.update(haltMonitor(payload(),true));assert(!history.permitsComparison());
        for(unsigned engine=0;engine<TGLHangCPU::Engines;++engine) {
            header[engine+2]=1;auto pending=haltMonitor(payload(),true);
            assert(pending.qualified && pending.observed && pending.mask==(1U<<engine));
            history={};history.update(pending);assert(!history.permitsComparison());
            header[engine+2]=2;
            assert(!haltMonitor(payload(),true).qualified);
            auto data=payload();TGLHangCPU::Capture capture{};
            capture.q[0]=TGLHangCPU::Schema;capture.q[1]=3;capture.q[7]=engine;
            [data appendBytes:&capture length:sizeof(capture)];
            const auto observed=haltMonitor(data,true);
            assert(observed.qualified && observed.observed && observed.mask==(1U<<engine));
            [data setLength:data.length-1];assert(!haltMonitor(data,true).qualified);
            data=payload();capture.q[7]=(engine+1)%TGLHangCPU::Engines;
            [data appendBytes:&capture length:sizeof(capture)];assert(!haltMonitor(data,true).qualified);
            header[engine+2]=3;assert(!haltMonitor(payload(),true).qualified);
            header[engine+2]=0;
            history.update(haltMonitor(payload(),true));assert(!history.permitsComparison());
        }
        auto extra=payload();[extra appendBytes:header length:1];assert(!haltMonitor(extra,true).qualified);
        header[0]=2;assert(!haltMonitor(payload(),true).qualified);header[0]=TGLHangCPU::Schema;
        header[1]++;assert(!haltMonitor(payload(),true).qualified);
        NSFileManager *fm=[NSFileManager defaultManager];
        NSString *root=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        NSString *active=[root stringByAppendingPathComponent:@"DiagnosticReports"];
        NSString *retired=[active stringByAppendingPathComponent:@"Retired"];
        NSArray<NSString *> *directories=@[active,retired];
        auto mkdir=[&](NSString *path) {
            assert([fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil]);
        };
        auto write=[&](NSString *path) {assert([[@"fixture" dataUsingEncoding:NSUTF8StringEncoding] writeToFile:path atomically:YES]);};
        auto remove=[&](NSString *path) {assert([fm removeItemAtPath:path error:nil]);};
        bool readable=true;
        assert(gpuReports(readable,directories).count==0 && !readable);
        mkdir(active);
        assert(gpuReports(readable,directories).count==0 && readable); // Archive may not exist.
        NSString *first=[active stringByAppendingPathComponent:@"first.gpuRestart"];
        write(first);write([active stringByAppendingPathComponent:@"unrelated.ips"]);
        auto reports=gpuReports(readable,directories);
        assert(readable && reports.count==1 && [reports containsObject:@"first.gpuRestart"]);
        mkdir(retired);
        assert([fm moveItemAtPath:first toPath:[retired stringByAppendingPathComponent:@"first.gpuRestart"] error:nil]);
        assert([gpuReports(readable,directories) isEqualToSet:reports] && readable);
        write([active stringByAppendingPathComponent:@"second.gpuRestart"]);
        assert(gpuReports(readable,directories).count==2 && readable);
        // A readable archive cannot hide loss of the required active directory.
        assert(gpuReports(readable,@[[root stringByAppendingPathComponent:@"missing"],retired]).count==1 && !readable);
        remove(retired);write(retired); // Existing archive is not readable as a directory.
        assert(gpuReports(readable,directories).count==1 && !readable);
        remove(root);
        puts("PASS: production report observer and optional pre-STOP parser; incomplete/unknown data rejected; no GPU");
    }
}
