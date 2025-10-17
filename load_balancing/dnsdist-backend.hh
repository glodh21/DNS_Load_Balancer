#pragma once
#include <memory>

struct DownstreamState;

namespace dnsdist::backend
{
void registerNewBackend(std::shared_ptr<DownstreamState>& backend);
}