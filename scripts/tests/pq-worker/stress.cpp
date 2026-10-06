#include "worker.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <atomic>
#include <initializer_list>
#include <sys/random.h>
#include <sched.h>
#include <unistd.h>
namespace pqqual { int native_execute(Op,Buffers&); }
static std::atomic<bool> rng_fail{false};
static std::atomic<unsigned> rng_call{0},rng_fail_at{0};
extern "C" int randombytes(uint8_t* p,size_t n) {
  if (rng_fail.load() || ++rng_call==rng_fail_at.load()) return -1;
  while (n) { auto r=getrandom(p,n,0); if (r<=0) return -1; p+=r; n-=size_t(r); }
  return 0;
}
using namespace pqqual;
using Clock=std::chrono::steady_clock;
static uint64_t ms() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()); }
static void pin() {
  cpu_set_t available; CPU_ZERO(&available);
  assert(sched_getaffinity(0,sizeof(available),&available)==0);
  for (int i=0;i<CPU_SETSIZE;++i) if (CPU_ISSET(i,&available)) {
    cpu_set_t one; CPU_ZERO(&one); CPU_SET(i,&one);
    assert(sched_setaffinity(0,sizeof(one),&one)==0); return;
  }
  assert(false);
}
int main() {
  pin(); Worker w; Buffers pair{},enc{},dec{},out{};
  // Initial entropy failure and a separate failure in KeyGen's PCT entropy call.
  for (unsigned fault : {1u,2u}) {
    rng_call=0; rng_fail_at=fault; Worker failing; Token token{}; Op op{};
    assert(failing.submit(Op::KeyGen,true,10000,nullptr,0,nullptr,0,token));
    auto* b=failing.begin(token,10000,op); assert(b);
    const int status=native_execute(op,*b); assert(status!=0);
    failing.finish(token,status,10001); assert(failing.clean());
    assert(!failing.take(token,10002,out)); assert(failing.clean());
  }
  rng_fail_at=0;
  {
    Worker abandoned; Token token{}; Op op{};
    assert(abandoned.submit(Op::KeyGen,true,10000,nullptr,0,nullptr,0,token));
    auto* b=abandoned.begin(token,10000,op); assert(b);
    assert(native_execute(op,*b)==0); abandoned.finish(token,0,10001);
    assert(abandoned.phase()==Phase::Result);
    abandoned.expire(15000); assert(abandoned.clean());
    assert(!abandoned.take(token,15000,out));
  }
  // Property/fault model: all operations still invoke the real primitive when begun.
  uint64_t time=10000; size_t completed=0,cancelled=0,rejected=0;
  double mean[3]{}, maximum[3]{}; size_t count[3]{};
  for (unsigned i=0;i<1000;++i) {
    assert(native_execute(Op::KeyGen,pair)==0);
    Worker worker;
    for (Op op : {Op::KeyGen,Op::Encaps,Op::Decaps}) {
      if (op==Op::Decaps) {
        std::memcpy(enc.pk,pair.pk,1568); assert(native_execute(Op::Encaps,enc)==0);
      }
      Token token{}; const auto* input=op==Op::KeyGen?nullptr:op==Op::Encaps?pair.pk:pair.sk;
      const size_t length=op==Op::KeyGen?0:op==Op::Encaps?1568:3168;
      const auto* ct=op==Op::Decaps?enc.ct:nullptr;
      const size_t cn=ct?1568:0;
      assert(!worker.submit(op,false,time,input,length,ct,cn,token)); ++rejected;
      assert(!worker.submit(static_cast<Op>(255),true,time,input,length,ct,cn,token)); ++rejected;
      assert(!worker.submit(op,true,time,input,length+1,ct,cn,token)); ++rejected;
      assert(worker.submit(op,true,time,input,length,ct,cn,token));
      Token other{}; assert(!worker.submit(op,true,time,input,length,ct,cn,other)); ++rejected;
      Op selected{};
      assert(!worker.begin({token.generation+1,token.ticket},time,selected));
      if (i%7==0) {
        worker.cancel(); ++cancelled; assert(worker.clean());
        assert(!worker.begin(token,time,selected)); time+=10000; continue;
      }
      auto* b=worker.begin(token,time,selected); assert(b);
      const bool retire=i%7==1;
      if (retire) worker.cancel();
      rng_fail=i%7==2 && op!=Op::Decaps;
      const auto start=Clock::now();
      const int status=native_execute(selected,*b);
      const double duration=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
      rng_fail=false;
      const auto index=static_cast<unsigned>(op); mean[index]+=duration;
      if (duration>maximum[index]) maximum[index]=duration;
      ++count[index];
      worker.finish(token,status,time+(i%7==3?5000:1));
      if (retire || i%7==3) { ++cancelled; assert(worker.clean()); assert(!worker.take(token,time,out)); }
      else {
        assert(!worker.take({token.generation+1,token.ticket},time,out));
        const bool ok=worker.take(token,time+2,out); assert(ok==(status==0));
        if (ok && op==Op::Decaps) assert(std::memcmp(out.ss,enc.ss,32)==0);
        if (ok) ++completed;
        assert(worker.clean()); out.clear();
      }
      worker.cancel(); assert(worker.clean()); time+=10000;
    }
    pair.clear(); enc.clear(); dec.clear();
  }
  // Queue/cancel/reset replay model: 1,000,000 transitions, no large allocation.
  uint32_t seed=123456789; Token current{}; Op selected{}; size_t transitions=0;
  for (;transitions<1000000;++transitions) {
    seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
    switch (seed%6) {
      case 0: w.submit(Op::KeyGen,true,time,nullptr,0,nullptr,0,current); break;
      case 1: w.cancel(); break;
      case 2: w.expire(time+6000); break;
      case 3: assert(!w.take({current.generation+1,current.ticket},time,out)); break;
      case 4: w.enqueue_failed(current); break;
      case 5: if (w.phase()==Phase::Queued) {
        auto* b=w.begin(current,time,selected);
        if (b) { b->ss[0]=123; w.cancel(); w.finish(current,0,time); }
        break;
      }
    }
    if (w.phase()==Phase::Empty) assert(w.clean());
    time+=2;
  }
  w.cancel(); assert(w.clean());
  // Same-CPU Linux scheduling simulation. Emulated UART/radio ticks continue
  // while a worker executes real KEM; this is NOT FreeRTOS or MCU validation.
  std::mutex mutex; std::condition_variable cv; Worker threaded;
  bool stop=false,queued=false,holding=false,release=false;
  Token queued_token{}; std::atomic<size_t> uart_ticks{0}; std::atomic<bool> service_stop{false};
  double service_gap=0;
  std::thread service([&] { pin(); auto prior=Clock::now();
    while (!service_stop) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1)); const auto now=Clock::now();
      const auto gap=std::chrono::duration<double,std::milli>(now-prior).count();
      if (gap>service_gap) service_gap=gap;
      prior=now; ++uart_ticks;
    }
  });
  std::thread thread([&] { pin(); std::unique_lock<std::mutex> lock(mutex);
    for (;;) {
      cv.wait(lock,[&]{return queued||stop;}); if (stop) break;
      const auto token=queued_token; queued=false;
      Op op{}; auto* b=threaded.begin(token,ms(),op); if (!b) { cv.notify_all(); continue; }
      holding=true; cv.notify_all(); cv.wait(lock,[&]{return release;}); holding=false; release=false;
      lock.unlock(); const int status=native_execute(op,*b); lock.lock();
      threaded.finish(token,status,ms()); cv.notify_all();
    }
  });
  for (size_t i=0;i<20;++i) {
    std::unique_lock<std::mutex> lock(mutex); Token token{};
    while (!threaded.submit(Op::KeyGen,true,ms(),nullptr,0,nullptr,0,token)) {
      lock.unlock(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); lock.lock();
    }
    queued_token=token; queued=true; cv.notify_all(); cv.wait(lock,[&]{return holding;});
    if (i%2==0) threaded.cancel(); // reset/radio loss at running boundary
    release=true; cv.notify_all();
    cv.wait(lock,[&]{return threaded.phase()!=Phase::Running && threaded.phase()!=Phase::Retiring;});
    if (i%2) assert(threaded.take(token,ms(),out));
    else assert(!threaded.take(token,ms(),out));
    assert(threaded.clean()); out.clear();
  }
  { std::lock_guard<std::mutex> lock(mutex); stop=true; cv.notify_all(); }
  thread.join(); service_stop=true; service.join();
  std::printf("{\"real_worker_operations\":%zu,\"completed\":%zu,\"cancelled_or_expired\":%zu,\"admission_rejects\":%zu,\"transitions\":%zu,\"threaded_operations\":20,\"simulated_uart_ticks\":%zu,\"linux_service_max_gap_ms\":%.6f,\"host_mean_ms\":[%.6f,%.6f,%.6f],\"host_max_ms\":[%.6f,%.6f,%.6f]}\n",
    count[0]+count[1]+count[2],completed,cancelled,rejected,transitions,uart_ticks.load(),service_gap,
    mean[0]/count[0],mean[1]/count[1],mean[2]/count[2],maximum[0],maximum[1],maximum[2]);
}
