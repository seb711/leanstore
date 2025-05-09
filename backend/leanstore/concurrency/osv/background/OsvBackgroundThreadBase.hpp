#pragma once
// -------------------------------------------------------------------------------------
#include "Exceptions.hpp"
// -------------------------------------------------------------------------------------
#include <sys/types.h>
#include <sys/resource.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <osv/jumpmu.hh>
// -------------------------------------------------------------------------------------
namespace mean
{
class OsvBackgroundThreadBase
{
  protected:
   std::string name;
   const int _id = -2;
   std::thread tWorker;

   int _process()
   {
      jumpmu::thread_local_jumpmu_ctx = new jumpmu::JumpMUContext{};
      setNameThisThread(name);
      int pid = getpid();
      int which = PRIO_PROCESS;
      int ret = process();
      delete jumpmu::thread_local_jumpmu_ctx; 
      return ret;
   }

  public:
  OsvBackgroundThreadBase(std::string name, int id) : name(name), _id(id) {}
  
   virtual ~OsvBackgroundThreadBase(){};

   OsvBackgroundThreadBase(const OsvBackgroundThreadBase& other) = delete;
   OsvBackgroundThreadBase(OsvBackgroundThreadBase&& other) = delete;
   OsvBackgroundThreadBase& operator=(const OsvBackgroundThreadBase& other) = delete;
   OsvBackgroundThreadBase& operator=(OsvBackgroundThreadBase&& other) = delete;

   virtual unsigned getPriority() = 0;
   virtual int process() = 0;

   void start()
   {
      tWorker = std::thread(&OsvBackgroundThreadBase::_process, this);
   }

   void join()
   {
      if (tWorker.joinable()) {
         tWorker.join();
      }
   }

   int id() const { return _id; }

   std::string getName() const { return name; }

   void setNameBeforeStart(std::string name) { this->name = name; }

   void setNameThisThread(std::string name)
   {
      this->name = name;
      posix_check(pthread_setname_np(pthread_self(), name.c_str()) == 0);
   }

   std::thread& thread_impl() {
      return tWorker;
   }
};
// -------------------------------------------------------------------------------------
}  // namespace mean
// -------------------------------------------------------------------------------------
