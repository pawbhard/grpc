#include <grpc/grpc.h>
#include <grpcpp/alarm.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include "examples/cpp/PingPong/pingpong.grpc.pb.h"
#include "examples/cpp/PingPong/pingpong.pb.h"

// Define time point type for convenience

class PingPongClient {
    public:
        explicit PingPongClient(std::shared_ptr<grpc::Channel> channel) : stub(pingpong::PingPongService::NewStub(channel)){
        }
    void SendPing() {
        pingpong::PingRequest request;
        request.set_ping(10);
        grpc::ClientContext ctx;
        pingpong::PongResponse response;
        std::mutex mu;
        bool done = false;
        grpc::Status status;
        std::condition_variable cv;
        auto start_time = absl::Now();
        absl::Duration latency;//= 0;
        stub->async()->SendPing(&ctx, &request, &response,
            [&mu, &cv, &done, &status, &start_time, &latency](grpc::Status s) {
                latency = absl::Now() - start_time; //absl::FDivDuration((absl::Now() - start_time),
                                              //absl::Microseconds(1));
                status = std::move(s);
                std::lock_guard<std::mutex> lock(mu);
                done = true;
                cv.notify_one();
              });
        std::unique_lock<std::mutex> lock(mu);
        while (!done) {
        cv.wait(lock);
        }
        std::cout<<"Latency in us: "<<latency<<"\n";
        if (status.ok()) {
            std::cout<<"RPC success response : "<<response.pong()<<"\n";
        } else {
            std::cerr<<"RPC failed "<<status.error_code()<<":"<<status.error_message()<<"\n";
        }
    }
    private:
        std::unique_ptr<pingpong::PingPongService::Stub> stub;
};


int main() {
    std::string server_addr = "0.0.0.0:50051";
    auto channel = grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());
    PingPongClient pc(channel);
    pc.SendPing();
    pc.SendPing();
    return 0;
}
