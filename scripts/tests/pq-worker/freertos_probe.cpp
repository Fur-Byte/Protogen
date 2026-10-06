// Uncalled qualification adapter. Relinked into separate, non-uploaded ELF files.
// Production app_main, protocol, task sizes and radio ingress are unchanged.
#include "worker.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#ifndef PQ_STACK_BYTES
#define PQ_STACK_BYTES 32768
#endif
static_assert(sizeof(StackType_t)==1,"ESP-IDF C3 task stack units must be bytes");
namespace pqqual { int native_execute(Op,Buffers&); }
namespace {
pqqual::Worker worker;
StaticTask_t task_control;
alignas(16) StackType_t task_stack[PQ_STACK_BYTES];
StaticQueue_t queue_control;
uint8_t queue_storage[sizeof(pqqual::Token)];
StaticSemaphore_t mutex_control;
QueueHandle_t queue=nullptr;
SemaphoreHandle_t mutex=nullptr;
TaskHandle_t task=nullptr;
uint64_t now_ms() { return uint64_t(esp_timer_get_time())/1000; }
struct Measurement {
  size_t before,after,minimum,largest,stack_free;
  uint64_t elapsed_us;
};
volatile Measurement samples[3]; // Numeric resource observations only.
void run(void*) {
  pqqual::Token token{};
  for (;;) {
    if (xQueueReceive(queue,&token,pdMS_TO_TICKS(100))!=pdTRUE) {
      // An abandoned completed result must not retain secrets indefinitely.
      xSemaphoreTake(mutex,portMAX_DELAY);
      worker.expire(now_ms());
      xSemaphoreGive(mutex);
      continue;
    }
    pqqual::Op op{};
    xSemaphoreTake(mutex,portMAX_DELAY);
    auto* buffers=worker.begin(token,now_ms(),op);
    xSemaphoreGive(mutex);
    if (!buffers) continue;
    auto& m=samples[static_cast<unsigned>(op)];
    m.before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    heap_caps_monitor_local_minimum_free_size_start();
    const auto start=esp_timer_get_time();
    // Never hold mutex/critical section across KEM; scheduler remains preemptible.
    const int status=pqqual::native_execute(op,*buffers);
    m.elapsed_us=uint64_t(esp_timer_get_time()-start);
    m.minimum=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    heap_caps_monitor_local_minimum_free_size_stop();
    m.largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m.after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m.stack_free=uxTaskGetStackHighWaterMark(nullptr); // ESP-IDF reports BYTES.
    xSemaphoreTake(mutex,portMAX_DELAY);
    worker.finish(token,status,now_ms());
    xSemaphoreGive(mutex);
    // A real blocked tick permits idle/watchdog servicing between operations.
    vTaskDelay(1);
  }
}
}
// ESP-IDF radio-enabled CSPRNG; this probe must only start AFTER Wi-Fi entropy
// is enabled. Production entropy/admission integration is explicitly pending.
extern "C" int randombytes(uint8_t* p,size_t n) { esp_fill_random(p,n); return 0; }
extern "C" bool pq_qualification_probe() {
  if (task) return true;
  mutex=xSemaphoreCreateMutexStatic(&mutex_control);
  queue=xQueueCreateStatic(1,sizeof(pqqual::Token),queue_storage,&queue_control);
  if (!mutex || !queue) return false;
  // Same priority as main: priority zero can be starved by current zero-tick delay.
  task=xTaskCreateStatic(run,"pq-qualification",PQ_STACK_BYTES,nullptr,1,task_stack,&task_control);
  return task!=nullptr;
}
extern "C" bool pq_qualification_submit(pqqual::Op op,bool admitted,
  const uint8_t* p,size_t n,const uint8_t* ct,size_t cn,pqqual::Token* token) {
  if (!task || !token) return false;
  xSemaphoreTake(mutex,portMAX_DELAY);
  worker.expire(now_ms());
  bool ok=worker.submit(op,admitted,now_ms(),p,n,ct,cn,*token);
  if (ok && xQueueSend(queue,token,0)!=pdTRUE) { worker.enqueue_failed(*token); ok=false; }
  xSemaphoreGive(mutex); return ok;
}
extern "C" void pq_qualification_cancel() {
  if (!task) return;
  xSemaphoreTake(mutex,portMAX_DELAY); worker.cancel(); xSemaphoreGive(mutex);
}
extern "C" bool pq_qualification_take(pqqual::Token token,pqqual::Buffers* out) {
  if (!task || !out) return false;
  xSemaphoreTake(mutex,portMAX_DELAY);
  const bool ok=worker.take(token,now_ms(),*out);
  xSemaphoreGive(mutex); return ok;
}
