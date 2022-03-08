// Generated by the gRPC C++ plugin.
// If you make any local change, they will be lost.
// source: remote/kv.proto

#include "remote/kv.pb.h"
#include "remote/kv.grpc.pb.h"

#include <functional>
#include <grpcpp/impl/codegen/async_stream.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/client_unary_call.h>
#include <grpcpp/impl/codegen/client_callback.h>
#include <grpcpp/impl/codegen/message_allocator.h>
#include <grpcpp/impl/codegen/method_handler.h>
#include <grpcpp/impl/codegen/rpc_service_method.h>
#include <grpcpp/impl/codegen/server_callback.h>
#include <grpcpp/impl/codegen/server_callback_handlers.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/impl/codegen/sync_stream.h>
namespace remote {

static const char* KV_method_names[] = {
  "/remote.KV/Version",
  "/remote.KV/Tx",
  "/remote.KV/StateChanges",
};

std::unique_ptr< KV::Stub> KV::NewStub(const std::shared_ptr< ::grpc::ChannelInterface>& channel, const ::grpc::StubOptions& options) {
  (void)options;
  std::unique_ptr< KV::Stub> stub(new KV::Stub(channel));
  return stub;
}

KV::Stub::Stub(const std::shared_ptr< ::grpc::ChannelInterface>& channel)
  : channel_(channel), rpcmethod_Version_(KV_method_names[0], ::grpc::internal::RpcMethod::NORMAL_RPC, channel)
  , rpcmethod_Tx_(KV_method_names[1], ::grpc::internal::RpcMethod::BIDI_STREAMING, channel)
  , rpcmethod_StateChanges_(KV_method_names[2], ::grpc::internal::RpcMethod::SERVER_STREAMING, channel)
  {}

::grpc::Status KV::Stub::Version(::grpc::ClientContext* context, const ::google::protobuf::Empty& request, ::types::VersionReply* response) {
  return ::grpc::internal::BlockingUnaryCall(channel_.get(), rpcmethod_Version_, context, request, response);
}

void KV::Stub::experimental_async::Version(::grpc::ClientContext* context, const ::google::protobuf::Empty* request, ::types::VersionReply* response, std::function<void(::grpc::Status)> f) {
  ::grpc_impl::internal::CallbackUnaryCall(stub_->channel_.get(), stub_->rpcmethod_Version_, context, request, response, std::move(f));
}

void KV::Stub::experimental_async::Version(::grpc::ClientContext* context, const ::grpc::ByteBuffer* request, ::types::VersionReply* response, std::function<void(::grpc::Status)> f) {
  ::grpc_impl::internal::CallbackUnaryCall(stub_->channel_.get(), stub_->rpcmethod_Version_, context, request, response, std::move(f));
}

void KV::Stub::experimental_async::Version(::grpc::ClientContext* context, const ::google::protobuf::Empty* request, ::types::VersionReply* response, ::grpc::experimental::ClientUnaryReactor* reactor) {
  ::grpc_impl::internal::ClientCallbackUnaryFactory::Create(stub_->channel_.get(), stub_->rpcmethod_Version_, context, request, response, reactor);
}

void KV::Stub::experimental_async::Version(::grpc::ClientContext* context, const ::grpc::ByteBuffer* request, ::types::VersionReply* response, ::grpc::experimental::ClientUnaryReactor* reactor) {
  ::grpc_impl::internal::ClientCallbackUnaryFactory::Create(stub_->channel_.get(), stub_->rpcmethod_Version_, context, request, response, reactor);
}

::grpc::ClientAsyncResponseReader< ::types::VersionReply>* KV::Stub::AsyncVersionRaw(::grpc::ClientContext* context, const ::google::protobuf::Empty& request, ::grpc::CompletionQueue* cq) {
  return ::grpc_impl::internal::ClientAsyncResponseReaderFactory< ::types::VersionReply>::Create(channel_.get(), cq, rpcmethod_Version_, context, request, true);
}

::grpc::ClientAsyncResponseReader< ::types::VersionReply>* KV::Stub::PrepareAsyncVersionRaw(::grpc::ClientContext* context, const ::google::protobuf::Empty& request, ::grpc::CompletionQueue* cq) {
  return ::grpc_impl::internal::ClientAsyncResponseReaderFactory< ::types::VersionReply>::Create(channel_.get(), cq, rpcmethod_Version_, context, request, false);
}

::grpc::ClientReaderWriter< ::remote::Cursor, ::remote::Pair>* KV::Stub::TxRaw(::grpc::ClientContext* context) {
  return ::grpc_impl::internal::ClientReaderWriterFactory< ::remote::Cursor, ::remote::Pair>::Create(channel_.get(), rpcmethod_Tx_, context);
}

void KV::Stub::experimental_async::Tx(::grpc::ClientContext* context, ::grpc::experimental::ClientBidiReactor< ::remote::Cursor,::remote::Pair>* reactor) {
  ::grpc_impl::internal::ClientCallbackReaderWriterFactory< ::remote::Cursor,::remote::Pair>::Create(stub_->channel_.get(), stub_->rpcmethod_Tx_, context, reactor);
}

::grpc::ClientAsyncReaderWriter< ::remote::Cursor, ::remote::Pair>* KV::Stub::AsyncTxRaw(::grpc::ClientContext* context, ::grpc::CompletionQueue* cq, void* tag) {
  return ::grpc_impl::internal::ClientAsyncReaderWriterFactory< ::remote::Cursor, ::remote::Pair>::Create(channel_.get(), cq, rpcmethod_Tx_, context, true, tag);
}

::grpc::ClientAsyncReaderWriter< ::remote::Cursor, ::remote::Pair>* KV::Stub::PrepareAsyncTxRaw(::grpc::ClientContext* context, ::grpc::CompletionQueue* cq) {
  return ::grpc_impl::internal::ClientAsyncReaderWriterFactory< ::remote::Cursor, ::remote::Pair>::Create(channel_.get(), cq, rpcmethod_Tx_, context, false, nullptr);
}

::grpc::ClientReader< ::remote::StateChangeBatch>* KV::Stub::StateChangesRaw(::grpc::ClientContext* context, const ::remote::StateChangeRequest& request) {
  return ::grpc_impl::internal::ClientReaderFactory< ::remote::StateChangeBatch>::Create(channel_.get(), rpcmethod_StateChanges_, context, request);
}

void KV::Stub::experimental_async::StateChanges(::grpc::ClientContext* context, ::remote::StateChangeRequest* request, ::grpc::experimental::ClientReadReactor< ::remote::StateChangeBatch>* reactor) {
  ::grpc_impl::internal::ClientCallbackReaderFactory< ::remote::StateChangeBatch>::Create(stub_->channel_.get(), stub_->rpcmethod_StateChanges_, context, request, reactor);
}

::grpc::ClientAsyncReader< ::remote::StateChangeBatch>* KV::Stub::AsyncStateChangesRaw(::grpc::ClientContext* context, const ::remote::StateChangeRequest& request, ::grpc::CompletionQueue* cq, void* tag) {
  return ::grpc_impl::internal::ClientAsyncReaderFactory< ::remote::StateChangeBatch>::Create(channel_.get(), cq, rpcmethod_StateChanges_, context, request, true, tag);
}

::grpc::ClientAsyncReader< ::remote::StateChangeBatch>* KV::Stub::PrepareAsyncStateChangesRaw(::grpc::ClientContext* context, const ::remote::StateChangeRequest& request, ::grpc::CompletionQueue* cq) {
  return ::grpc_impl::internal::ClientAsyncReaderFactory< ::remote::StateChangeBatch>::Create(channel_.get(), cq, rpcmethod_StateChanges_, context, request, false, nullptr);
}

KV::Service::Service() {
  AddMethod(new ::grpc::internal::RpcServiceMethod(
      KV_method_names[0],
      ::grpc::internal::RpcMethod::NORMAL_RPC,
      new ::grpc::internal::RpcMethodHandler< KV::Service, ::google::protobuf::Empty, ::types::VersionReply>(
          [](KV::Service* service,
             ::grpc_impl::ServerContext* ctx,
             const ::google::protobuf::Empty* req,
             ::types::VersionReply* resp) {
               return service->Version(ctx, req, resp);
             }, this)));
  AddMethod(new ::grpc::internal::RpcServiceMethod(
      KV_method_names[1],
      ::grpc::internal::RpcMethod::BIDI_STREAMING,
      new ::grpc::internal::BidiStreamingHandler< KV::Service, ::remote::Cursor, ::remote::Pair>(
          [](KV::Service* service,
             ::grpc_impl::ServerContext* ctx,
             ::grpc_impl::ServerReaderWriter<::remote::Pair,
             ::remote::Cursor>* stream) {
               return service->Tx(ctx, stream);
             }, this)));
  AddMethod(new ::grpc::internal::RpcServiceMethod(
      KV_method_names[2],
      ::grpc::internal::RpcMethod::SERVER_STREAMING,
      new ::grpc::internal::ServerStreamingHandler< KV::Service, ::remote::StateChangeRequest, ::remote::StateChangeBatch>(
          [](KV::Service* service,
             ::grpc_impl::ServerContext* ctx,
             const ::remote::StateChangeRequest* req,
             ::grpc_impl::ServerWriter<::remote::StateChangeBatch>* writer) {
               return service->StateChanges(ctx, req, writer);
             }, this)));
}

KV::Service::~Service() {
}

::grpc::Status KV::Service::Version(::grpc::ServerContext* context, const ::google::protobuf::Empty* request, ::types::VersionReply* response) {
  (void) context;
  (void) request;
  (void) response;
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}

::grpc::Status KV::Service::Tx(::grpc::ServerContext* context, ::grpc::ServerReaderWriter< ::remote::Pair, ::remote::Cursor>* stream) {
  (void) context;
  (void) stream;
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}

::grpc::Status KV::Service::StateChanges(::grpc::ServerContext* context, const ::remote::StateChangeRequest* request, ::grpc::ServerWriter< ::remote::StateChangeBatch>* writer) {
  (void) context;
  (void) request;
  (void) writer;
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}


}  // namespace remote
