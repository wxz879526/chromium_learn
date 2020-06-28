// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <iostream>
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/logging_win.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread.h"
#include "base/threading/thread_task_runner_handle.h"

#include "base/task_scheduler/task_scheduler.h"
#include "base/threading/thread.h"

#include "net/base/ip_endpoint.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"

class MyHttpServer : public net::HttpServer::Delegate {
 public:
  void OnConnect(int connection_id) override {
    DCHECK(connection_map_.find(connection_id) == connection_map_.end());
    connection_map_[connection_id] = true;
  }
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override {
    requests_.push_back(std::make_pair(info, connection_id));
    if (requests_.size() == quit_after_request_count_)
      run_loop_quit_func_.Run();
  }

  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override {
    NOTREACHED();
  }

  void OnWebSocketMessage(int connection_id, const std::string& data) {
    NOTREACHED();
  }

  void OnClose(int connection_id) override {
    DCHECK(connection_map_.find(connection_id) != connection_map_.end());
    connection_map_[connection_id] = false;
    if (connection_id == quit_on_close_connection_)
      run_loop_quit_func_.Run();
  }

 private:
  std::unordered_map<int, bool> connection_map_;
  std::vector<std::pair<net::HttpServerRequestInfo, int>> requests_;
  base::Closure run_loop_quit_func_;
  net::IPEndPoint server_address_;
  size_t quit_after_request_count_;
  int quit_on_close_connection_;
};

void SubPump() {
  base::MessageLoop::current()->SetNestableTasksAllowed(true);
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  base::MessageLoop::current()->QuitWhenIdle();
}

int main() {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(0, nullptr);

  base::FilePath log_file_path;
  base::PathService::Get(base::DIR_EXE, &log_file_path);
  log_file_path = log_file_path.AppendASCII("my.log");

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_ALL;
  settings.log_file = log_file_path.value().c_str();
  logging::InitLogging(settings);
  logging::SetMinLogLevel(logging::LOG_VERBOSE);

  VLOG(1) << "Exe Started....";

  base::MessageLoop message_loop(base::MessageLoop::TYPE_UI);
  base::RunLoop run_loop;
  //message_loop.SetNestableTasksAllowed(true);
  message_loop.task_runner()->PostTask(FROM_HERE, base::BindOnce(SubPump));
  message_loop.task_runner()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce([]() { std::cout << "sjsijsisjisj" << std::endl; }),
      base::TimeDelta::FromSeconds(10));
  run_loop.Run();

  // MyHttpServer srv;

  printf("Hello, world.\n");
  return 0;
}
