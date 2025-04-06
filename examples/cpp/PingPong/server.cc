#include <grpcpp/support/status.h>
#include <grpcpp/server_builder.h>
#include <iostream>
#include <memory>

#include "examples/cpp/PingPong/pingpong.grpc.pb.h"


class PingPongServiceImpl : public pingpong::PingPongService::CallbackService {
    public:
    
        grpc::ServerUnaryReactor* SendPing(grpc::CallbackServerContext* ctx,
                                        const pingpong::PingRequest* ping,
                                        pingpong::PongResponse* response) override
        {
            response->set_pong(ping->ping());
            response->set_pong(10);
            auto *reactor = ctx->DefaultReactor();
            reactor->Finish(grpc::Status::OK);
            return reactor;
        }
};

void RunServer()
{
    std::string server_addr("0.0.0.0:50051");
    PingPongServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server (builder.BuildAndStart());
    std::cout << "Server listening on " << server_addr << std::endl;
    server->Wait();
}

int main() {
    RunServer();
    return 0;
}