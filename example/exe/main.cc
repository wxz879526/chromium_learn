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
#include "base/memory/ref_counted.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread_task_runner_handle.h"

#include "base/threading/thread.h"
#include "base/task_scheduler/task_scheduler.h"

#include "ipc/ipc_message.h"
#include "ipc/ipc_message_utils.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_channel_mojo.h"
#include "ipc/ipc_sync_channel.h"

#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/scoped_ipc_support.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/outgoing_broker_client_invitation.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"

#define IPC_MESSAGE_IMPL
#include "ipc/ipc_message_macros.h"

#include "IPCMessage.h"

class Worker : public IPC::Listener, public IPC::Sender {
 public:
  Worker(IPC::Channel::Mode mode, const std::string &thread_name,
	  mojo::ScopedMessagePipeHandle channel_handle) 
	  : done_(new base::WaitableEvent(base::WaitableEvent::ResetPolicy::AUTOMATIC, 
		  base::WaitableEvent::InitialState::NOT_SIGNALED)),
        channel_created_(new base::WaitableEvent(base::WaitableEvent::ResetPolicy::AUTOMATIC,
			base::WaitableEvent::InitialState::NOT_SIGNALED)),
        mode_(mode),
        channel_handle_(std::move(channel_handle)),
        ipc_thread_((thread_name + "_ipc").c_str()),
        listener_thread_((thread_name + "_listener").c_str()),
        overrided_thread_(nullptr),
        shutdown_event_(base::WaitableEvent::ResetPolicy::MANUAL, base::WaitableEvent::InitialState::NOT_SIGNALED),
        is_shutdown_(false)
		{}

  Worker(mojo::ScopedMessagePipeHandle channel_handle ,IPC::Channel::Mode mode)
      : done_(new base::WaitableEvent(
            base::WaitableEvent::ResetPolicy::AUTOMATIC,
            base::WaitableEvent::InitialState::NOT_SIGNALED)),
        channel_created_(new base::WaitableEvent(
            base::WaitableEvent::ResetPolicy::AUTOMATIC,
            base::WaitableEvent::InitialState::NOT_SIGNALED)),
        mode_(mode),
        channel_handle_(std::move(channel_handle)),
        ipc_thread_("ipc thread"),
        listener_thread_("listener thread"),
        overrided_thread_(nullptr),
        shutdown_event_(base::WaitableEvent::ResetPolicy::MANUAL,
                        base::WaitableEvent::InitialState::NOT_SIGNALED),
        is_shutdown_(false) {}

  ~Worker() override {
	  CHECK(is_shutdown_);
  }

  void AddRef() {}

  void Release() {}

  IPC::Channel::Mode mode() { return mode_; }

  bool Send(IPC::Message* msg) override { 
	  return channel_->Send(msg);
  }

  void WaitForChannelCreation() { return channel_created_->Wait(); }

  void CloseChannel()
  { 
	DCHECK(ListenerThread()->task_runner()->BelongsToCurrentThread());
    channel_->Close();
  }

  void OnListenerThreadShutdown1(base::WaitableEvent *listener_event,
	  base::WaitableEvent* ipc_event)
  {
    channel_.reset();

	base::RunLoop().RunUntilIdle();
        ipc_thread_.task_runner()->PostTask(FROM_HERE, base::Bind(&Worker::OnIPCThreadShutdown, this, listener_event, ipc_event));
  }

  void OnIPCThreadShutdown(base::WaitableEvent *listener_event, base::WaitableEvent *ipc_event)
  {
    base::RunLoop().RunUntilIdle();
    ipc_event->Signal();
    listener_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&Worker::OnListenerThreadShutdown2, this, listener_event));
  }

  void OnListenerThreadShutdown2(base::WaitableEvent* listener_event) {
    base::RunLoop().RunUntilIdle();
    listener_event->Signal();
  }

  void Shutdown() {
    base::WaitableEvent listener_done(
        base::WaitableEvent::ResetPolicy::AUTOMATIC,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    base::WaitableEvent ipc_done(
        base::WaitableEvent::ResetPolicy::AUTOMATIC,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    ListenerThread()->task_runner()->PostTask(
        FROM_HERE, base::Bind(&Worker::OnListenerThreadShutdown1, this,
                              &listener_done, &ipc_done));
    listener_done.Wait();
    ipc_done.Wait();
    ipc_thread_.Stop();
    listener_thread_.Stop();
    is_shutdown_ = true;
  }

  base::WaitableEvent* done_event() { return done_.get(); }

  void Start()
  {
    StartThread(&listener_thread_, base::MessageLoop::TYPE_DEFAULT);
    ListenerThread()->task_runner()->PostTask(
        FROM_HERE, base::Bind(&Worker::OnStart, this));
  }

  void StartThread(base::Thread *thread, base::MessageLoop::Type type)
  {
    base::Thread::Options options;
    options.message_loop_type = type;
    thread->StartWithOptions(options);
  }

  mojo::MessagePipeHandle TakeChannelHandle() {
    DCHECK(channel_handle_.is_valid());
    return channel_handle_.release();
  }

  virtual IPC::SyncChannel* CreateChannel() {
    std::unique_ptr<IPC::SyncChannel> channel = IPC::SyncChannel::Create(
        TakeChannelHandle(), mode_, this, ipc_thread_.task_runner(), true,
        &shutdown_event_);

	return channel.release();
  }

  base::Thread* ListenerThread() {
    return overrided_thread_ ? overrided_thread_ : &listener_thread_;
  }

  void Done() { return done_->Signal(); }

  const base::Thread& ipc_thread() const { return ipc_thread_; }

  bool SendAnswerToLife(bool pump, bool succeed) { 
	  int answer = 0;
      IPC::SyncMessage* msg = new SyncChannelTestMsg_AnswerToLife(&answer);
      if (pump)
         msg->EnableMessagePumping();
      bool result = Send(msg);
      DCHECK_EQ(result, succeed);
      DCHECK_EQ(answer, (succeed ? 42 : 0));
      return result;
  }

  virtual void OnAnswer(int* answer) { NOTREACHED(); }
  virtual void OnAnswerDelay(IPC::Message* reply_msg) { 
	 int answer;
     OnAnswer(&answer);
     SyncChannelTestMsg_AnswerToLife::WriteReplyParams(reply_msg, answer);
     Send(reply_msg);
  }

  virtual bool OnMessageReceived(const IPC::Message& message) override
  {
    IPC_BEGIN_MESSAGE_MAP(Worker, message)
      IPC_MESSAGE_HANDLER_DELAY_REPLY(SyncChannelTestMsg_AnswerToLife, OnAnswerDelay)
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

  protected:
	IPC::SyncChannel* channel() { return channel_.get(); }
	virtual void Run() {}

  private:
	void OnStart()
	{ 
		  StartThread(&ipc_thread_, base::MessageLoop::TYPE_IO);
          channel_.reset(CreateChannel());
          channel_created_->Signal();
          Run();
	 }

protected:
  std::unique_ptr<base::WaitableEvent> done_;
  std::unique_ptr<base::WaitableEvent> channel_created_;
  mojo::ScopedMessagePipeHandle channel_handle_;
  IPC::Channel::Mode mode_;
  std::unique_ptr<IPC::SyncChannel> channel_;
  base::Thread ipc_thread_;
  base::Thread listener_thread_;
  base::Thread* overrided_thread_;

  base::WaitableEvent shutdown_event_;

  bool is_shutdown_;

  DISALLOW_COPY_AND_ASSIGN(Worker);
};

class SimpleServer : public Worker {
 public:
	 SimpleServer(bool pump_during_send, mojo::ScopedMessagePipeHandle channel_handle)
      : Worker(IPC::Channel::MODE_SERVER, "simple_server", std::move(channel_handle)),
        pump_during_send_(pump_during_send)
	 {

	 }

	 void Run() override { 
		 SendAnswerToLife(pump_during_send_, true);
         Done();
	 }

	 bool pump_during_send_;
};

class SimpleClient : public Worker {
 public:
	 explicit SimpleClient(mojo::ScopedMessagePipeHandle channel_handle)
		 : Worker(std::move(channel_handle), IPC::Channel::MODE_CLIENT)
	 {

     }

	 void OnAnswer(int *answer)
	 { 
		 *answer = 42;
         Done();
	 }
};

void RunTest(std::vector<Worker*> workers) {
	for (size_t i = 0; i < workers.size(); ++i)
	{
		if (workers[i]->mode() & IPC::Channel::MODE_SERVER_FLAG) {
			workers[i]->Start();
			workers[i]->WaitForChannelCreation();
		}
	}

	for (size_t i = 0; i < workers.size(); ++i)
	{
          if (workers[i]->mode() & IPC::Channel::MODE_CLIENT_FLAG) {
            workers[i]->Start();
		  }
	}

	for (size_t i = 0; i < workers.size(); ++i)
	{
          workers[i]->done_event()->Wait();
	}

	for (size_t i = 0; i < workers.size(); ++i)
	{
          workers[i]->Shutdown();
	}
}
void Simple(bool pump_during_send) {
  std::vector<Worker*> workers;
  mojo::MessagePipe pipe;
  workers.push_back(
      new SimpleServer(pump_during_send, std::move(pipe.handle0)));
  workers.push_back(new SimpleClient(std::move(pipe.handle1)));
  RunTest(workers);
}

class A : public base::RefCounted<A> {
 public:
  A() {}
  ~A() {}
};

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

  { 
	  scoped_refptr<A> aa = base::MakeRefCounted<A>();
  }

  base::TaskScheduler::CreateAndStartWithDefaultParams("main");

  base::MessageLoop messageLoop;
  base::RunLoop runloop;
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind([](){
    std::cout << "hello world in task" << std::endl;
  }));
  base::PostTaskWithTraits(FROM_HERE, {base::TaskPriority::USER_BLOCKING}, base::Bind([]() {
	  std::cout << "working in task scheduler thread" << std::endl;
  }));
  auto taskRunner = base::CreateTaskRunnerWithTraits({base::TaskPriority::BACKGROUND});
  taskRunner->PostTask(FROM_HERE, base::Bind([]() {
	  std::cout << "working in task scheduler thread2"<< std::endl;
  }));
  auto taskRunner3 =
      base::CreateTaskRunnerWithTraits({base::TaskPriority::USER_BLOCKING});
  taskRunner3->PostTask(FROM_HERE, base::Bind([]() {
                         std::cout << "working in task scheduler thread3"
                                   << std::endl;
                       }));
  runloop.Run();

  //attach a message to current thread
  /*base::MessageLoop message_loop;

  base::RunLoop run_loop;

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, 
	  base::BindOnce([](){ printf("hello, do task in task queue\n");
	  }));

  run_loop.Run();*/

  /*mojo::edk::Init();
  SimpleListener clientListener;
  base::MessageLoopForIO main_messageLoop;

  std::cout << "Client: Creating IPC Channel" << kFuzzerChannel << std::endl;
  mojo::ScopedMessagePipeHandle handle_;
  auto channel = IPC::Channel::Create(
      handle_, IPC::Channel::MODE_SERVER, &clientListener);


  if (!channel->Connect())
  {
    std::cout << "Error in connecting to the channel" << kFuzzerChannel
              << std::endl;
    return -1;
  }

  std::cout << "Client : Connected to IPC channel " << kFuzzerChannel
            << std::endl;

  std::cout << "Initializing listener" << std::endl;
  clientListener.Init(channel.get());

  std::cout << "Starting the MessageLoop" << std::endl;
  base::RunLoop run_loop;
  run_loop.Run();*/

  base::MessageLoop loop;
  mojo::edk::Init();
  base::Thread ipc_thread("ipc");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
  mojo::edk::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::edk::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  /*mojo::edk::PlatformChannelPair channel;
  mojo::edk::OutgoingBrokerClientInvitation invitation;
  mojo::ScopedMessagePipeHandle pipe =
      invitation.AttachMessagePipe("pipe-name");

  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  base::FilePath executable_path;
  base::PathService::Get(base::DIR_MODULE, &executable_path);
  base::FilePath client_path = executable_path.AppendASCII("ipc_child.exe");
  command_line.SetProgram(client_path);

  base::LaunchOptions options;
  mojo::edk::HandlePassingInformation hpInfo;
  channel.PrepareToPassClientHandleToChildProcess(&command_line, &hpInfo);
  options.handles_to_inherit = &hpInfo;
  mojo::edk::ScopedPlatformHandle server_handle;
  server_handle = channel.PassServerHandle();
  base::Process child_process = base::LaunchProcess(command_line, options);
  channel.ChildProcessLaunched();

  mojo::edk::ProcessErrorCallback callback;
  invitation.Send(
      child_process.Handle(),
      mojo::edk::ConnectionParams(mojo::edk::TransportProtocol::kLegacy,
                                  std::move(server_handle)),
      callback);

  base::RunLoop().Run();*/

  Simple(true);

  printf("Hello, world.\n");
  return 0;
}
