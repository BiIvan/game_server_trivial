#pragma once

#include <iostream>
#include <string_view>
#include <boost/json.hpp>
#include <boost/log/core.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/expressions.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include "literals.h"

namespace json = boost::json;
namespace logging = boost::log;
namespace sinks = boost::log::sinks;

BOOST_LOG_ATTRIBUTE_KEYWORD( additional_data, "AdditionalData", json::value)
BOOST_LOG_ATTRIBUTE_KEYWORD( timestamp, "TimeStamp", boost::posix_time::ptime)

namespace logger {

  inline void InitLogger() {
    static const bool initialized = [] {
      logging::add_common_attributes();
      using Backend = sinks::text_ostream_backend;
      using Sink = sinks::synchronous_sink<Backend>;
      auto sink = boost::make_shared<Sink>();
      sink->locked_backend()->add_stream( 
        boost::shared_ptr<std::ostream>( &std::cout, boost::null_deleter{}));
      sink->locked_backend()->auto_flush(true);
      sink->set_formatter([]( const logging::record_view& record
        , logging::formatting_ostream& stream) {
        json::object log_record;
        const auto timestamp_value = record[timestamp];
        if (timestamp_value) {
          log_record[TIMEST] = boost::posix_time::to_iso_extended_string( timestamp_value.get());
        }
        const auto message_value = record[boost::log::expressions::smessage];
        log_record[MSG] = message_value ? std::string(message_value.get()) : std::string{};
        const auto data_value = record[additional_data];
        log_record[DATA] = data_value && data_value.get().is_object() 
          ? data_value.get() 
          : json::value{json::object{}};
        stream << json::serialize(log_record) << "\n";
      });
      logging::core::get()->add_sink(sink);
      return true;
    }();
  (void)initialized;
  }

  inline void LogError( const boost::system::error_code& ec, std::string_view where) {
  BOOST_LOG_TRIVIAL(error)
    << logging::add_value( additional_data
         , json::object{ {COD, ec.value()}, {TXT, ec.message()}, {WHE, where},})
    << ERR;
  }

}  // namespace logger

