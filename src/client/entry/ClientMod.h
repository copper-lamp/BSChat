#pragma once
#include <memory>
#include "client/entry/ClientRuntime.h"
namespace vc::client { class ClientMod final { public: ClientMod(std::unique_ptr<ClientRuntime> runtime):runtime_(std::move(runtime)){} bool load(){return bool(runtime_);} bool enable(){if(!runtime_)return false;runtime_->start();return true;} bool disable(){if(runtime_)runtime_->stop();return true;} bool unload(){return disable();} private: std::unique_ptr<ClientRuntime> runtime_; }; }
