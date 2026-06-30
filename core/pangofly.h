#ifndef PANGOFLY_PANGOFLY_H_
#define PANGOFLY_PANGOFLY_H_

#include <memory>
#include <string>

#include "core/node/node.h"

namespace pangofly {

std::unique_ptr<Node> CreateNode(const std::string& node_name, const std::string& name_space = "");

bool Init(const char* argv0 = nullptr);
void Shutdown();

} // namespace pangofly

#endif // PANGOFLY_PANGOFLY_H_