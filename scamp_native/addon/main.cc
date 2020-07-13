#include "Networking.h"
#include "PartOne.h"
#include <cassert>
#include <memory>
#include <napi.h>

namespace {
inline NiPoint3 NapiValueToNiPoint3(Napi::Value v)
{
  NiPoint3 res;
  auto arr = v.As<Napi::Array>();
  int n = std::min((int)arr.Length(), 3);
  for (int i = 0; i < n; ++i)
    res[i] = arr.Get(i).As<Napi::Number>().FloatValue();
  return res;
}
}

class ScampServerListener;

class ScampServer : public Napi::ObjectWrap<ScampServer>
{
  friend class ScampServerListener;

public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  ScampServer(const Napi::CallbackInfo& info);

  Napi::Value Tick(const Napi::CallbackInfo& info);
  Napi::Value On(const Napi::CallbackInfo& info);
  Napi::Value CreateActor(const Napi::CallbackInfo& info);
  Napi::Value SetUserActor(const Napi::CallbackInfo& info);
  Napi::Value GetUserActor(const Napi::CallbackInfo& info);
  Napi::Value DestroyActor(const Napi::CallbackInfo& info);

private:
  std::unique_ptr<PartOne> partOne;
  std::shared_ptr<Networking::IServer> server;
  std::shared_ptr<ScampServerListener> listener;
  std::unique_ptr<Napi::Env> tickEnv;
  Napi::ObjectReference emitter;
  Napi::FunctionReference emit;

  static Napi::FunctionReference constructor;
};

class ScampServerListener : public PartOne::Listener
{
public:
  ScampServerListener(ScampServer& server_)
    : server(server_)
  {
  }

  void OnConnect(Networking::UserId userId) override
  {
    assert(server.tickEnv);
    auto env = *server.tickEnv;
    auto emit = server.emit.Value();
    emit.Call(
      server.emitter.Value(),
      { Napi::String::New(env, "connect"), Napi::Number::New(env, userId) });
  }

  void OnDisconnect(Networking::UserId userId) override
  {
    assert(server.tickEnv);
    auto env = *server.tickEnv;
    auto emit = server.emit.Value();
    emit.Call(server.emitter.Value(),
              { Napi::String::New(env, "disconnect"),
                Napi::Number::New(env, userId) });
  }

  void OnCustomPacket(Networking::UserId userId,
                      const simdjson::dom::element& content) override
  {
    std::string contentStr = simdjson::minify(content);

    assert(server.tickEnv);
    auto env = *server.tickEnv;
    auto emit = server.emit.Value();
    emit.Call(server.emitter.Value(),
              { Napi::String::New(env, "customPacket"),
                Napi::Number::New(env, userId),
                Napi::String::New(env, contentStr) });
  }

private:
  ScampServer& server;
};

Napi::FunctionReference ScampServer::constructor;

Napi::Object ScampServer::Init(Napi::Env env, Napi::Object exports)
{
  Napi::Function func = DefineClass(
    env, "ScampServer",
    { InstanceMethod<&ScampServer::Tick>("tick"),
      InstanceMethod<&ScampServer::On>("on"),
      InstanceMethod<&ScampServer::CreateActor>("createActor"),
      InstanceMethod<&ScampServer::SetUserActor>("setUserActor"),
      InstanceMethod<&ScampServer::GetUserActor>("getUserActor"),
      InstanceMethod<&ScampServer::DestroyActor>("destroyActor") });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("ScampServer", func);
  return exports;
}

ScampServer::ScampServer(const Napi::CallbackInfo& info)
  : ObjectWrap(info)
{
  partOne.reset(new PartOne);
  listener.reset(new ScampServerListener(*this));
  partOne->AddListener(listener);

  Napi::Number port = info[0].As<Napi::Number>(),
               maxConnections = info[1].As<Napi::Number>();
  server = Networking::CreateServer(static_cast<uint32_t>(port),
                                    static_cast<uint32_t>(maxConnections));

  auto res =
    info.Env().RunScript("let require = global.require || "
                         "global.process.mainModule.constructor._load; let "
                         "Emitter = require('events'); new Emitter");
  emitter = Napi::Persistent(res.As<Napi::Object>());
  emit = Napi::Persistent(emitter.Value().Get("emit").As<Napi::Function>());
}

Napi::Value ScampServer::Tick(const Napi::CallbackInfo& info)
{
  tickEnv.reset(new Napi::Env(info.Env()));
  partOne->pushedSendTarget = server.get();
  server->Tick(PartOne::HandlePacket, partOne.get());
  return info.Env().Undefined();
}

Napi::Value ScampServer::On(const Napi::CallbackInfo& info)
{
  auto on = emitter.Get("on").As<Napi::Function>();
  on.Call(emitter.Value(), { info[0], info[1] });
  return info.Env().Undefined();
}

Napi::Value ScampServer::CreateActor(const Napi::CallbackInfo& info)
{
  auto formId = info[0].As<Napi::Number>().Uint32Value();
  auto pos = NapiValueToNiPoint3(info[1]);
  auto angleZ = info[2].As<Napi::Number>().FloatValue();
  auto cellOrWorld = info[3].As<Napi::Number>().Uint32Value();
  try {
    partOne->CreateActor(formId, pos, angleZ, cellOrWorld, server.get());
  } catch (std::exception& e) {
    throw Napi::Error::New(info.Env(), (std::string)e.what());
  }
  return info.Env().Undefined();
}

Napi::Value ScampServer::SetUserActor(const Napi::CallbackInfo& info)
{
  auto userId = info[0].As<Napi::Number>().Uint32Value();
  auto actorFormId = info[1].As<Napi::Number>().Uint32Value();
  try {
    partOne->SetUserActor(userId, actorFormId, server.get());
  } catch (std::exception& e) {
    throw Napi::Error::New(info.Env(), (std::string)e.what());
  }
  return info.Env().Undefined();
}

Napi::Value ScampServer::GetUserActor(const Napi::CallbackInfo& info)
{
  auto userId = info[0].As<Napi::Number>().Uint32Value();
  try {
    return Napi::Number::New(info.Env(), partOne->GetUserActor(userId));
  } catch (std::exception& e) {
    throw Napi::Error::New(info.Env(), (std::string)e.what());
  }
  return info.Env().Undefined();
}

Napi::Value ScampServer::DestroyActor(const Napi::CallbackInfo& info)
{
  auto actorFormId = info[0].As<Napi::Number>().Uint32Value();
  try {
    partOne->DestroyActor(actorFormId);
  } catch (std::exception& e) {
    throw Napi::Error::New(info.Env(), (std::string)e.what());
  }
  return info.Env().Undefined();
}

Napi::String Method(const Napi::CallbackInfo& info)
{
  Napi::Env env = info.Env();
  return Napi::String::New(env, "world");
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  return ScampServer::Init(env, exports);
}

NODE_API_MODULE(scamp, Init)
