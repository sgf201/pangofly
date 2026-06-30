#ifndef PANGOFLY_COMMON_LOG_H_
#define PANGOFLY_COMMON_LOG_H_

#include <iostream>
#include <string>

namespace pangofly {

#define PANGOFLY_LOG(level) std::cerr << "[" #level "] "
#define PANGOFLY_INFO PANGOFLY_LOG(INFO)
#define PANGOFLY_WARN PANGOFLY_LOG(WARN)
#define PANGOFLY_ERROR PANGOFLY_LOG(ERROR)
#define PANGOFLY_DEBUG PANGOFLY_LOG(DEBUG)

} // namespace pangofly

#endif // PANGOFLY_COMMON_LOG_H_