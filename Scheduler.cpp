/**
 * @file Scheduler.cpp
 * @version 1.2
 *
 * @section License
 * Copyright (C) 2015-2016, Mikael Patel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "Scheduler.h"
#include <Arduino.h>

// Configuration: SRAM and heap handling
#if defined(TEENSYDUINO) && defined(__MK20DX256__)
#undef ARDUINO_ARCH_AVR
#define TEENSY_ARCH_ARM
#define RAMEND 0x20008000

#elif defined(ARDUINO_ARCH_AVR)
extern int __heap_start, *__brkval;
extern char* __malloc_heap_end;
extern size_t __malloc_margin;

#elif defined(ARDUINO_ARCH_SAM)
#define RAMEND 0x20088000

#elif defined(ARDUINO_ARCH_SAMD)
#define RAMEND 0x20008000

#elif defined(ESP8266)
// pretty sure ESP8266 has more RAM than this, but we're not actually
// managing our own stacks anyway (cont.h defines 1k stacks always)
#define RAMEND 0x20008000
#endif

// Single-ton
SchedulerClass Scheduler;

// Main task and run queue
SchedulerClass::task_t SchedulerClass::s_main = {
  &SchedulerClass::s_main,
  &SchedulerClass::s_main,
#ifdef ESP8266
  NULL,
#else
  { 0 },
#endif
  NULL
};

// Reference running task
SchedulerClass::task_t* SchedulerClass::s_running = &SchedulerClass::s_main;

// Initial top stack for task allocation
size_t SchedulerClass::s_top = SchedulerClass::DEFAULT_STACK_SIZE;

bool SchedulerClass::begin(size_t stackSize)
{
  // Set main task stack size
  s_top = stackSize;
  return (true);
}

bool SchedulerClass::start(func_t taskSetup, func_t taskLoop, size_t stackSize)
{
  // Check called from main task and valid task loop function
  if ((s_running != &s_main) || (taskLoop == NULL)) return (false);

#ifndef ESP8266
  // Adjust stack size with size of task context
  stackSize += sizeof(task_t);

  // Allocate stack(s) and check if main stack top should be set
  size_t frame = RAMEND - (size_t) &frame;
  uint8_t stack[s_top - frame];
  if (s_main.stack == NULL) s_main.stack = stack;
#endif

#if defined(ARDUINO_ARCH_AVR)
  // Check that the task can be allocated
  int HEAPEND = (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
  int STACKSTART = ((int) stack) - stackSize;
  HEAPEND += __malloc_margin;
  if (STACKSTART < HEAPEND) return (false);

  // Adjust heap limit
  __malloc_heap_end = (char*) STACKSTART;
#endif

#if defined(ARDUINO_ARCH_SAM)  || \
    defined(ARDUINO_ARCH_SAMD) || \
    defined(TEENSY_ARCH_ARM)
  // Check that the task can be allocated
  if (s_top + stackSize > STACK_MAX) return (false);
#endif

#ifndef ESP8266
  // Adjust stack top for next task allocation
  s_top += stackSize;
#endif

  // Initiate task with given functions and stack top
  init(taskSetup, taskLoop, 
#ifdef ESP8266
    0);
#else
    stack - stackSize);
#endif

  return (true);
}

void SchedulerClass::yield()
{
#ifdef ESP8266
  cont_run(s_running->context, s_running->topFunc);
  // Next task in run queue will continue
  s_running = s_running->next;
#else
  // Caller will continue here on yield
  if (setjmp(s_running->context)) return;

  // Next task in run queue will continue
  s_running = s_running->next;
  longjmp(s_running->context, true);
#endif
}

#ifndef ESP8266
size_t SchedulerClass::stack()
{
  unsigned char marker;
  return (&marker - s_running->stack);
}
#endif

void SchedulerClass::init(func_t setup, func_t loop, const uint8_t* stack)
{
  // Add task last in run queue (main task)
  task_t task;
  task.next = &s_main;
  task.prev = s_main.prev;
  s_main.prev->next = &task;
  s_main.prev = &task;
#ifndef ESP8266
  task.stack = stack;
#endif

#ifndef ESP8266
  // Create context for new task, caller will return
  if (setjmp(task.context)) {
    if (setup != NULL) setup();
    while (1) loop();
  }
#endif
}

#ifdef ESP8266
void esp_schedule();
void esp_yield();
#endif

extern "C"
void yield(void)
{
  Scheduler.yield();
#ifdef ESP8266
  esp_schedule();
  esp_yield();
#endif
}
