// SPDX-License-Identifier: MIT
// Exercise the production filesystem observer. The renamed capture entry point
// is never called: no IOKit access, GPU creation or real report directories.
#define main captureMainNotInvoked
#include "../tools/tgl-capture.mm"
#undef main
#include <cassert>
int main() {
    @autoreleasepool {
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
        puts("PASS: production GPU report observer; absent active, optional archive, retirement, new report and unreadable archive; no GPU");
    }
}
