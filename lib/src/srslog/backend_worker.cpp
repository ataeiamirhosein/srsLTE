/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "backend_worker.h"
#include "srslte/srslog/sink.h"
#include <cassert>

using namespace srslog;

void backend_worker::stop()
{
  // Signal the worker thread to stop.
  running_flag = false;
  if (worker_thread.joinable()) {
    worker_thread.join();
  }
}

void backend_worker::create_worker()
{
  assert(!running_flag && "Only one worker thread should be created");

  std::thread t([this]() {
    running_flag = true;
    do_work();
  });

  worker_thread = std::move(t);

  // Block the caller thread until we are signaled that the worker is running.
  while (!running_flag) {
    std::this_thread::sleep_for(std::chrono::microseconds(10));
  }
}

void backend_worker::start()
{
  // Ensure we only create the worker thread once.
  std::call_once(start_once_flag, [this]() { create_worker(); });
}

void backend_worker::do_work()
{
  assert(running_flag && "Thread entry function called without running thread");

  while (running_flag) {
    auto item = queue.timed_pop(sleep_period_ms);

    // Spin again when the timeout expires.
    if (!item.first) {
      continue;
    }

    report_queue_on_full();

    process_log_entry(std::move(item.second));
  }

  // When we reach here, the thread is about to terminate, last chance to
  // process the last log entries.
  process_outstanding_entries();
}

void backend_worker::process_log_entry(detail::log_entry&& entry)
{
  std::string result = fmt::vsprintf(entry.fmtstring, std::move(entry.store));
  detail::memory_buffer buffer(result);

  if (auto err_str = entry.s->write(buffer)) {
    err_handler(err_str.get_error());
  }
}

void backend_worker::process_outstanding_entries()
{
  assert(!running_flag &&
         "Cannot process outstanding entries while thread is running");

  while (true) {
    auto item = queue.timed_pop(1);

    // Check if the queue is empty.
    if (!item.first) {
      break;
    }

    process_log_entry(std::move(item.second));
  }
}