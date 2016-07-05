// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/associated_group.h"
#include "mojo/public/cpp/bindings/associated_interface_ptr.h"
#include "mojo/public/cpp/bindings/associated_interface_ptr_info.h"
#include "mojo/public/cpp/bindings/associated_interface_request.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/interfaces/bindings/tests/test_associated_interfaces.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace test {
namespace {

class TestTaskRunner : public base::SingleThreadTaskRunner {
 public:
  TestTaskRunner()
      : thread_id_(base::PlatformThread::CurrentRef()),
        quit_called_(false),
        task_ready_(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                    base::WaitableEvent::InitialState::NOT_SIGNALED) {}

  bool PostNonNestableDelayedTask(const tracked_objects::Location& from_here,
                                  const base::Closure& task,
                                  base::TimeDelta delay) override {
    NOTREACHED();
    return false;
  }

  bool PostDelayedTask(const tracked_objects::Location& from_here,
                       const base::Closure& task,
                       base::TimeDelta delay) override {
    {
      base::AutoLock locker(lock_);
      tasks_.push(task);
    }
    task_ready_.Signal();
    return true;
  }
  bool RunsTasksOnCurrentThread() const override {
    return base::PlatformThread::CurrentRef() == thread_id_;
  }

  // Only quits when Quit() is called.
  void Run() {
    DCHECK(RunsTasksOnCurrentThread());
    quit_called_ = false;

    while (true) {
      {
        base::AutoLock locker(lock_);
        while (!tasks_.empty()) {
          auto task = tasks_.front();
          tasks_.pop();

          {
            base::AutoUnlock unlocker(lock_);
            task.Run();
            if (quit_called_)
              return;
          }
        }
      }
      task_ready_.Wait();
    }
  }

  void Quit() {
    DCHECK(RunsTasksOnCurrentThread());
    quit_called_ = true;
  }

  // Waits until one task is ready and runs it.
  void RunOneTask() {
    DCHECK(RunsTasksOnCurrentThread());

    while (true) {
      {
        base::AutoLock locker(lock_);
        if (!tasks_.empty()) {
          auto task = tasks_.front();
          tasks_.pop();

          {
            base::AutoUnlock unlocker(lock_);
            task.Run();
            return;
          }
        }
      }
      task_ready_.Wait();
    }
  }

 private:
  ~TestTaskRunner() override {}

  const base::PlatformThreadRef thread_id_;
  bool quit_called_;
  base::WaitableEvent task_ready_;

  // Protect |tasks_|.
  base::Lock lock_;
  std::queue<base::Closure> tasks_;

  DISALLOW_COPY_AND_ASSIGN(TestTaskRunner);
};

template <typename BindingType, typename RequestType>
class IntegerSenderImpl : public IntegerSender {
 public:
  IntegerSenderImpl(RequestType request,
                    scoped_refptr<base::SingleThreadTaskRunner> runner)
      : binding_(this, std::move(request), std::move(runner)) {}

  ~IntegerSenderImpl() override {}

  using EchoHandler = Callback<void(int32_t, const EchoCallback&)>;
  void set_echo_handler(const EchoHandler& handler) { echo_handler_ = handler; }

  void Echo(int32_t value, const EchoCallback& callback) override {
    if (echo_handler_.is_null())
      callback.Run(value);
    else
      echo_handler_.Run(value, callback);
  }
  void Send(int32_t value) override { NOTREACHED(); }

  BindingType* binding() { return &binding_; }

 private:
  BindingType binding_;
  EchoHandler echo_handler_;
};

class IntegerSenderConnectionImpl : public IntegerSenderConnection {
 public:
  using SenderType = IntegerSenderImpl<AssociatedBinding<IntegerSender>,
                                       IntegerSenderAssociatedRequest>;

  explicit IntegerSenderConnectionImpl(
      IntegerSenderConnectionRequest request,
      scoped_refptr<base::SingleThreadTaskRunner> runner,
      scoped_refptr<base::SingleThreadTaskRunner> sender_runner)
      : binding_(this, std::move(request), std::move(runner)),
        sender_runner_(std::move(sender_runner)) {}

  ~IntegerSenderConnectionImpl() override {}

  void set_get_sender_notification(const Closure& notification) {
    get_sender_notification_ = notification;
  }
  void GetSender(IntegerSenderAssociatedRequest sender) override {
    sender_impl_.reset(new SenderType(std::move(sender), sender_runner_));
    get_sender_notification_.Run();
  }

  void AsyncGetSender(const AsyncGetSenderCallback& callback) override {
    NOTREACHED();
  }

  Binding<IntegerSenderConnection>* binding() { return &binding_; }

  SenderType* sender_impl() { return sender_impl_.get(); }

 private:
  Binding<IntegerSenderConnection> binding_;
  std::unique_ptr<SenderType> sender_impl_;
  scoped_refptr<base::SingleThreadTaskRunner> sender_runner_;
  Closure get_sender_notification_;
};

class BindTaskRunnerTest : public testing::Test {
 protected:
  void SetUp() override {
    binding_task_runner_ = scoped_refptr<TestTaskRunner>(new TestTaskRunner);
    ptr_task_runner_ = scoped_refptr<TestTaskRunner>(new TestTaskRunner);

    auto request = GetProxy(&ptr_, ptr_task_runner_);
    impl_.reset(new ImplType(std::move(request), binding_task_runner_));
  }

  base::MessageLoop loop_;
  scoped_refptr<TestTaskRunner> binding_task_runner_;
  scoped_refptr<TestTaskRunner> ptr_task_runner_;

  IntegerSenderPtr ptr_;
  using ImplType =
      IntegerSenderImpl<Binding<IntegerSender>, IntegerSenderRequest>;
  std::unique_ptr<ImplType> impl_;
};

class AssociatedBindTaskRunnerTest : public testing::Test {
 protected:
  void SetUp() override {
    connection_binding_task_runner_ =
        scoped_refptr<TestTaskRunner>(new TestTaskRunner);
    connection_ptr_task_runner_ =
        scoped_refptr<TestTaskRunner>(new TestTaskRunner);
    sender_binding_task_runner_ =
        scoped_refptr<TestTaskRunner>(new TestTaskRunner);
    sender_ptr_task_runner_ = scoped_refptr<TestTaskRunner>(new TestTaskRunner);

    auto connection_request =
        GetProxy(&connection_ptr_, connection_ptr_task_runner_);
    connection_impl_.reset(new IntegerSenderConnectionImpl(
        std::move(connection_request), connection_binding_task_runner_,
        sender_binding_task_runner_));

    connection_impl_->set_get_sender_notification(
        [this]() { connection_binding_task_runner_->Quit(); });

    connection_ptr_->GetSender(GetProxy(&sender_ptr_,
                                        connection_ptr_.associated_group(),
                                        sender_ptr_task_runner_));
    connection_binding_task_runner_->Run();
  }

  base::MessageLoop loop_;
  scoped_refptr<TestTaskRunner> connection_binding_task_runner_;
  scoped_refptr<TestTaskRunner> connection_ptr_task_runner_;
  scoped_refptr<TestTaskRunner> sender_binding_task_runner_;
  scoped_refptr<TestTaskRunner> sender_ptr_task_runner_;

  IntegerSenderConnectionPtr connection_ptr_;
  std::unique_ptr<IntegerSenderConnectionImpl> connection_impl_;
  IntegerSenderAssociatedPtr sender_ptr_;
};

TEST_F(BindTaskRunnerTest, MethodCall) {
  bool echo_called = false;
  impl_->set_echo_handler(
      [&, this](int32_t value, const IntegerSender::EchoCallback& callback) {
        EXPECT_EQ(1024, value);
        echo_called = true;
        callback.Run(value);
        binding_task_runner_->Quit();
      });

  bool echo_replied = false;
  ptr_->Echo(1024, [&, this](int32_t value) {
    EXPECT_EQ(1024, value);
    echo_replied = true;
    ptr_task_runner_->Quit();
  });
  binding_task_runner_->Run();
  EXPECT_TRUE(echo_called);
  ptr_task_runner_->Run();
  EXPECT_TRUE(echo_replied);
}

TEST_F(BindTaskRunnerTest, BindingConnectionError) {
  bool connection_error_called = false;
  impl_->binding()->set_connection_error_handler([&, this]() {
    connection_error_called = true;
    binding_task_runner_->Quit();
  });

  ptr_.reset();
  binding_task_runner_->Run();
  EXPECT_TRUE(connection_error_called);
}

TEST_F(BindTaskRunnerTest, PtrConnectionError) {
  bool connection_error_called = false;
  ptr_.set_connection_error_handler([&, this]() {
    connection_error_called = true;
    ptr_task_runner_->Quit();
  });

  impl_->binding()->Close();
  ptr_task_runner_->Run();
  EXPECT_TRUE(connection_error_called);
}

TEST_F(AssociatedBindTaskRunnerTest, MethodCall) {
  bool echo_called = false;
  connection_impl_->sender_impl()->set_echo_handler(
      [&](int32_t value, const IntegerSender::EchoCallback& callback) {
        EXPECT_EQ(1024, value);
        echo_called = true;
        callback.Run(value);
      });

  bool echo_replied = false;
  sender_ptr_->Echo(1024, [&](int32_t value) {
    EXPECT_EQ(1024, value);
    echo_replied = true;
  });

  // The Echo request first arrives at the master endpoint's task runner, and
  // then is forwarded to the associated endpoint's task runner.
  connection_binding_task_runner_->RunOneTask();
  sender_binding_task_runner_->RunOneTask();
  EXPECT_TRUE(echo_called);

  // Similarly, the Echo response arrives at the master endpoint's task runner
  // and then is forwarded to the associated endpoint's task runner.
  connection_ptr_task_runner_->RunOneTask();
  sender_ptr_task_runner_->RunOneTask();
  EXPECT_TRUE(echo_replied);
}

TEST_F(AssociatedBindTaskRunnerTest, BindingConnectionError) {
  bool sender_impl_error = false;
  connection_impl_->sender_impl()->binding()->set_connection_error_handler(
      [&, this]() {
        sender_impl_error = true;
        sender_binding_task_runner_->Quit();
      });

  bool connection_impl_error = false;
  connection_impl_->binding()->set_connection_error_handler([&, this]() {
    connection_impl_error = true;
    connection_binding_task_runner_->Quit();
  });

  bool sender_ptr_error = false;
  sender_ptr_.set_connection_error_handler([&, this]() {
    sender_ptr_error = true;
    sender_ptr_task_runner_->Quit();
  });
  connection_ptr_.reset();
  sender_ptr_task_runner_->Run();
  EXPECT_TRUE(sender_ptr_error);
  connection_binding_task_runner_->Run();
  EXPECT_TRUE(connection_impl_error);
  sender_binding_task_runner_->Run();
  EXPECT_TRUE(sender_impl_error);
}

TEST_F(AssociatedBindTaskRunnerTest, PtrConnectionError) {
  bool sender_impl_error = false;
  connection_impl_->sender_impl()->binding()->set_connection_error_handler(
      [&, this]() {
        sender_impl_error = true;
        sender_binding_task_runner_->Quit();
      });

  bool connection_ptr_error = false;
  connection_ptr_.set_connection_error_handler([&, this]() {
    connection_ptr_error = true;
    connection_ptr_task_runner_->Quit();
  });

  bool sender_ptr_error = false;
  sender_ptr_.set_connection_error_handler([&, this]() {
    sender_ptr_error = true;
    sender_ptr_task_runner_->Quit();
  });
  connection_impl_->binding()->Close();
  sender_binding_task_runner_->Run();
  EXPECT_TRUE(sender_impl_error);
  connection_ptr_task_runner_->Run();
  EXPECT_TRUE(connection_ptr_error);
  sender_ptr_task_runner_->Run();
  EXPECT_TRUE(sender_ptr_error);
}

}  // namespace
}  // namespace test
}  // namespace mojo