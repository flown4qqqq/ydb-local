#pragma once

#include <yt/yt/core/logging/log_manager.h>

#include <yt/yt/core/misc/public.h>

namespace NYT::NKafka {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_GLOBAL(const NLogging::TLogger, KafkaLogger, NLogging::TLogger("Kafka").WithMinLevel(NLogging::ELogLevel::Trace));

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NKafka
