#include "net/http.hpp"

#import <Foundation/Foundation.h>

namespace ote::net {

Response getBackend(const std::string& url, const std::string& userAgent) {
    Response res;
    @autoreleasepool {
        static NSURLSession* session = [] {
            NSURLSessionConfiguration* cfg = [NSURLSessionConfiguration defaultSessionConfiguration];
            cfg.HTTPMaximumConnectionsPerHost = 8;
            cfg.timeoutIntervalForRequest = 15;
            cfg.URLCache = nil;
            return [NSURLSession sessionWithConfiguration:cfg];
        }();
        NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!u) {
            res.error = "bad url";
            return res;
        }
        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:u];
        [req setValue:[NSString stringWithUTF8String:userAgent.c_str()] forHTTPHeaderField:@"User-Agent"];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block Response* out = &res;
        NSURLSessionDataTask* task = [session dataTaskWithRequest:req
                                                completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                                                    if (error) {
                                                        out->error = error.localizedDescription.UTF8String;
                                                    } else {
                                                        out->status = (int)((NSHTTPURLResponse*)response).statusCode;
                                                        const auto* p = static_cast<const std::uint8_t*>(data.bytes);
                                                        out->body.assign(p, p + data.length);
                                                    }
                                                    dispatch_semaphore_signal(done);
                                                }];
        [task resume];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    }
    return res;
}

void setJavaVm(void*) {}

}
