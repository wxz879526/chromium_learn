// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <tchar.h>
#include <iostream>
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/logging_win.h"
#include "base/path_service.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread_task_runner_handle.h"

#include "ipc/ipc_message.h"
#include "ipc/ipc_message_utils.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_channel_mojo.h"

#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"

#define IPC_MESSAGE_IMPL
#include "ipc/ipc_message_macros.h"

#include "IPCMessage.h"

class SimpleListener : public IPC::Listener {
 public:
  SimpleListener() : other_(nullptr) {}

  void Init(IPC::Sender *s)
  { 
	  other_ = s;
  }

  virtual bool OnMessageReceived(const IPC::Message& msg) override
  {
	  std::cout << "Listener::OnMessageReceived()" << std::endl;
      IPC_BEGIN_MESSAGE_MAP(SimpleListener, msg)
        IPC_MESSAGE_HANDLER(MsgClassIS, classIS)
        IPC_MESSAGE_HANDLER(MsgClassSI, classSI)
      IPC_END_MESSAGE_MAP()
      return true;
  }

  virtual void OnChannelConnected(int32_t peer_pid)
  {
	  std::cout << "Listener::OnChannelConnected()" << std::endl;
  }

  virtual void OnChannelError() 
  { 
	  std::cout << "Listener::OnChannelError" << std::endl;
      exit(0);
  }

  void classIS(int myInt, std::wstring str)
  {
    std::cout << "classIS(): Received ==> Int :" << myInt << " String :" << str
              << std::endl;
  }

   void classSI(std::wstring mystring, int myint) {
    std::cout << "classSI(): Received ==> String :" << mystring << " Int :" << myint << std::endl;
  }

 protected:
  IPC::Sender* other_;
};

const char kFuzzerChannel[] = "channelName";

int main() {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(0, nullptr);

  base::FilePath log_file_path;
  base::PathService::Get(base::DIR_EXE, &log_file_path);
  log_file_path = log_file_path.AppendASCII("my2.log");

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_ALL;
  settings.log_file = log_file_path.value().c_str();
  logging::InitLogging(settings);
  logging::SetMinLogLevel(logging::LOG_VERBOSE);

  VLOG(1) << "Exe Started....";

  //attach a message to current thread
  /*base::MessageLoop message_loop;

  base::RunLoop run_loop;

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, 
	  base::BindOnce([](){ printf("hello, do task in task queue\n");
	  }));

  run_loop.Run();*/

  mojo::edk::Init();
  SimpleListener serverListener;
  base::MessageLoopForIO main_messageLoop;

  std::cout << "SERVER: Creating IPC Channel" << kFuzzerChannel << std::endl;
  mojo::ScopedMessagePipeHandle handle_;
  auto channel = IPC::ChannelMojo::Create(
      std::move(handle_), IPC::Channel::MODE_CLIENT, &serverListener);


  if (!channel->Connect())
  {
    std::cout << "Error in connecting to the channel" << kFuzzerChannel
              << std::endl;
    return -1;
  }

  std::cout << "Client : Connected to IPC channel " << kFuzzerChannel
            << std::endl;

  std::cout << "Initializing listener" << std::endl;
  serverListener.Init(channel.get());

  std::cout << "Starting the MessageLoop" << std::endl;
  base::RunLoop run_loop;
  run_loop.Run();

  printf("Hello, world.\n");
  return 0;
}
